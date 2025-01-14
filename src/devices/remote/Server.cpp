// Copyright 2023-2024 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include <anari/anari_cpp.hpp>
#include <functional>
#include <iostream>
#include <sstream>
#include "ArrayInfo.h"
#include "Buffer.h"
#include "Compression.h"
#include "Logging.h"
#include "ObjectDesc.h"
#include "async/connection.h"
#include "async/connection_manager.h"
#include "async/work_queue.h"
#include "common.h"

using namespace std::placeholders;

// Global variables
static std::string g_libraryType = "environment";
static ANARILibrary g_library = nullptr;
static bool g_verbose = false;
static unsigned short g_port = 31050;

namespace remote {

void statusFunc(const void *userData,
    ANARIDevice device,
    ANARIObject source,
    ANARIDataType sourceType,
    ANARIStatusSeverity severity,
    ANARIStatusCode code,
    const char *message)
{
  bool verbose = *(const bool *)userData;
  if (severity == ANARI_SEVERITY_FATAL_ERROR) {
    fprintf(stderr, "[FATAL] %s\n", message);
  } else if (severity == ANARI_SEVERITY_ERROR) {
    fprintf(stderr, "[ERROR] %s\n", message);
  } else if (severity == ANARI_SEVERITY_WARNING) {
    fprintf(stderr, "[WARN ] %s\n", message);
  }

  if (!verbose)
    return;

  if (severity == ANARI_SEVERITY_PERFORMANCE_WARNING) {
    fprintf(stderr, "[PERF ] %s\n", message);
  } else if (severity == ANARI_SEVERITY_INFO) {
    fprintf(stderr, "[INFO ] %s\n", message);
  } else if (severity == ANARI_SEVERITY_DEBUG) {
    fprintf(stderr, "[DEBUG] %s\n", message);
  }
}

static ANARIObject newObject(
    ANARIDevice dev, ANARIDataType type, std::string subtype)
{
  if (type == ANARI_LIGHT) {
    return anariNewLight(dev, subtype.c_str());
  } else if (type == ANARI_CAMERA) {
    return anariNewCamera(dev, subtype.c_str());
  } else if (type == ANARI_GEOMETRY) {
    return anariNewGeometry(dev, subtype.c_str());
  } else if (type == ANARI_SPATIAL_FIELD) {
    return anariNewSpatialField(dev, subtype.c_str());
  } else if (type == ANARI_SURFACE) {
    return anariNewSurface(dev);
  } else if (type == ANARI_VOLUME) {
    return anariNewVolume(dev, subtype.c_str());
  } else if (type == ANARI_MATERIAL) {
    return anariNewMaterial(dev, subtype.c_str());
  } else if (type == ANARI_SAMPLER) {
    return anariNewSampler(dev, subtype.c_str());
  } else if (type == ANARI_GROUP) {
    return anariNewGroup(dev);
  } else if (type == ANARI_INSTANCE) {
    return anariNewInstance(dev, subtype.c_str());
  } else if (type == ANARI_WORLD) {
    return anariNewWorld(dev);
  } else if (type == ANARI_FRAME) {
    return anariNewFrame(dev);
  } else if (type == ANARI_RENDERER) {
    return anariNewRenderer(dev, subtype.c_str());
  }

  return nullptr;
}

static ANARIArray newArray(
    ANARIDevice dev, const ArrayInfo &info, const uint8_t *data)
{
  ANARIArray array = nullptr;
  if (info.type == ANARI_ARRAY1D) {
    array = anariNewArray1D(
        dev, nullptr, nullptr, nullptr, info.elementType, info.numItems1);
  } else if (info.type == ANARI_ARRAY2D) {
    array = anariNewArray2D(dev,
        nullptr,
        nullptr,
        nullptr,
        info.elementType,
        info.numItems1,
        info.numItems2);
  } else if (info.type == ANARI_ARRAY3D) {
    array = anariNewArray3D(dev,
        nullptr,
        nullptr,
        nullptr,
        info.elementType,
        info.numItems1,
        info.numItems2,
        info.numItems3);
  }

  if (array && data) {
    void *ptr = anariMapArray(dev, array);
    memcpy(ptr, data, info.getSizeInBytes());
    anariUnmapArray(dev, array);
  }

  return array;
}

struct ResourceManager
{
  // Device handles are generated by us and returned to the client
  Handle registerDevice(ANARIDevice dev)
  {
    Handle handle = nextDeviceHandle++;
    size_t newNumHandles = std::max(anariDevices.size(), (size_t)handle + 1);
    anariDevices.resize(newNumHandles);
    anariDevices[handle] = dev;
    registeredObjects.resize(newNumHandles);
    registeredArrays.resize(newNumHandles);
    registerObject(handle, handle, dev, ANARI_DEVICE);
    return handle;
  }

  // Object handles are generated by the clients
  void registerObject(uint64_t deviceID,
      uint64_t objectID,
      ANARIObject anariObj,
      ANARIDataType type)
  {
    size_t newNumHandles =
        std::max(registeredObjects[deviceID].size(), (size_t)objectID + 1);

    registeredObjects[deviceID].resize(newNumHandles);
    registeredObjects[deviceID][objectID] = {
        anariDevices[deviceID], anariObj, type};
  }

  // Like registerObject, but stores array size; so we can later
  // send the whole array data back to the client on mapArray()
  void registerArray(uint64_t deviceID,
      uint64_t objectID,
      ANARIObject anariObj,
      const ArrayInfo &info)
  {
    registerObject(deviceID, objectID, anariObj, info.type);

    size_t newNumHandles =
        std::max(registeredArrays[deviceID].size(), (size_t)objectID + 1);

    registeredArrays[deviceID].resize(newNumHandles);
    registeredArrays[deviceID][objectID] = info;
  }

  ANARIDevice getDevice(Handle deviceHandle)
  {
    if (deviceHandle >= anariDevices.size())
      return nullptr;

    return anariDevices[deviceHandle];
  }

  ObjectDesc getObjectDesc(Handle deviceHandle, Handle objectHandle)
  {
    if (deviceHandle >= registeredObjects.size())
      return {};

    if (objectHandle >= registeredObjects[deviceHandle].size())
      return ObjectDesc(anariDevices[deviceHandle], nullptr);

    return registeredObjects[deviceHandle][objectHandle];
  }

  ArrayInfo getArrayInfo(Handle deviceHandle, Handle objectHandle)
  {
    if (deviceHandle >= registeredArrays.size()
        || objectHandle >= registeredArrays[deviceHandle].size())
      return {};

    return registeredArrays[deviceHandle][objectHandle];
  }

  Handle nextDeviceHandle = 1;

  std::vector<ANARIDevice> anariDevices;

  // vector of anari objects per device
  std::vector<std::vector<ObjectDesc>> registeredObjects;

  // vector of array infos per device
  std::vector<std::vector<ArrayInfo>> registeredArrays;
};

struct Server
{
  // For now only one client:
  struct
  {
    CompressionFeatures compression;
  } client;

  ResourceManager resourceManager;
  async::connection_manager_pointer manager;
  async::connection_pointer conn;
  async::work_queue queue;

  explicit Server(unsigned short port = 31050)
      : manager(async::make_connection_manager(port))
  {
    logging::Initialize();

    g_library = anariLoadLibrary(g_libraryType.c_str(), statusFunc, &g_verbose);
  }

  ~Server()
  {
    anariUnloadLibrary(g_library);
  }

  void accept()
  {
    LOG(logging::Level::Info) << "Server: accepting...";

    manager->accept(std::bind(&Server::handleNewConnection,
        this,
        std::placeholders::_1,
        std::placeholders::_2));
  }

  void run()
  {
    manager->run_in_thread();
    queue.run_in_thread();
  }

  void wait()
  {
    manager->wait();
  }

  void write(unsigned type, std::shared_ptr<Buffer> buf)
  {
    queue.post(std::bind(&Server::writeImpl, this, type, buf));
  }

  void writeImpl(unsigned type, std::shared_ptr<Buffer> buf)
  {
    conn->write(type, *buf);
  }

  std::vector<uint8_t> translateArrayData(
      Buffer &buf, ANARIDevice dev, ArrayInfo info)
  {
    std::vector<uint8_t> arrayData(info.getSizeInBytes());
    buf.read((char *)arrayData.data(), arrayData.size());

    // Translate remote to device handles
    if (anari::isObject(info.elementType)) {
      const auto &registeredObjects =
          resourceManager.registeredObjects[(uint64_t)dev];

      size_t numObjects = info.numItems1 * std::max(uint64_t(1), info.numItems2)
          * std::max(uint64_t(1), info.numItems3);

      // This only works b/c sizeof(ANARIObject)==sizeof(uint64_t)!
      // TODO: can this cause issues with alignment on some platforms?!
      const uint64_t *handles = (const uint64_t *)arrayData.data();
      ANARIObject *objects = (ANARIObject *)arrayData.data();

      for (size_t i = 0; i < numObjects; ++i) {
        objects[i] = registeredObjects[handles[i]].object;
      }
    }
    return arrayData;
  }

  bool handleNewConnection(
      async::connection_pointer new_conn, boost::system::error_code const &e)
  {
    if (e) {
      LOG(logging::Level::Error)
          << "Server: could not connect to client: " << e.message();
      manager->stop();
      return false;
    }

    LOG(logging::Level::Info) << "server: connected";

    // Accept and save this connection
    // and set the message handler of the connection
    conn = new_conn;
    conn->set_handler(std::bind(&Server::handleMessage,
        this,
        std::placeholders::_1,
        std::placeholders::_2,
        std::placeholders::_3));

    // Accept new connections (TODO: the new connection
    // will overwrite the current one, so, store these
    // in a list of connections)
    accept();

    return true;
  }

  void handleMessage(async::connection::reason reason,
      async::message_pointer message,
      boost::system::error_code const &e)
  {
    if (e) {
      LOG(logging::Level::Error) << "Server: error: " << e.message();

      manager->stop();
      return;
    }

    LOG(logging::Level::Info)
        << "Message: " << toString(message->type())
        << ", message size: " << prettyBytes(message->size());

#define CHECK(obj, errorMessage)                                               \
  if (!obj) {                                                                  \
    LOG(logging::Level::Error) << errorMessage;                                \
    return;                                                                    \
  }

    if (reason == async::connection::Read) {
      // Buffer with all the inputs
      auto inputBuffer =
          std::make_shared<Buffer>(message->data(), message->size());

      // Receive common object information:
      ObjectDesc remoteObj, serverObj;
      inputBuffer->read(remoteObj);

      // Translate to handles compatible with the underlying device:
      serverObj = resourceManager.getObjectDesc(
          (Handle)remoteObj.device, (Handle)remoteObj.object);
      // Bring these in sync, in case the object wasn't registered yet:
      serverObj.type = remoteObj.type;
      serverObj.subtype = remoteObj.subtype;

      if (message->type() == MessageType::NewDevice) {
        std::string deviceType;
        inputBuffer->read(deviceType);
        inputBuffer->read(client.compression);

        ANARIDevice dev = anariNewDevice(g_library, deviceType.c_str());
        Handle deviceHandle = resourceManager.registerDevice(dev);
        CompressionFeatures cf = getCompressionFeatures();

        // return device handle and other info to client
        auto outputBuffer = std::make_shared<Buffer>();
        outputBuffer->write(deviceHandle);
        outputBuffer->write(cf);
        write(MessageType::DeviceHandle, outputBuffer);

        LOG(logging::Level::Info)
            << "Creating new device, type: " << deviceType
            << ", device ID: " << deviceHandle << ", ANARI handle: " << dev;
        LOG(logging::Level::Info)
            << "Client has TurboJPEG: " << client.compression.hasTurboJPEG;
        LOG(logging::Level::Info)
            << "Client has SNAPPY: " << client.compression.hasSNAPPY;
      } else if (message->type() == MessageType::NewObject) {
        CHECK(serverObj.device, "Error on anariNewObject: invalid device");

        ANARIObject anariObj =
            newObject(serverObj.device, serverObj.type, serverObj.subtype);

        resourceManager.registerObject((Handle)remoteObj.device,
            (Handle)remoteObj.object,
            anariObj,
            remoteObj.type);

        LOG(logging::Level::Info)
            << "Creating new object, objectID: " << remoteObj.object
            << ", ANARI handle: " << anariObj;
      } else if (message->type() == MessageType::NewArray) {
        CHECK(serverObj.device, "Error on anariNewArray: invalid device");

        ArrayInfo info;
        info.type = serverObj.type;

        inputBuffer->read(info.elementType);
        inputBuffer->read(info.numItems1);
        inputBuffer->read(info.numItems2);
        inputBuffer->read(info.numItems3);

        std::vector<uint8_t> arrayData;
        if (inputBuffer->pos < message->size()) {
          arrayData = translateArrayData(*inputBuffer, remoteObj.device, info);
        }

        ANARIArray anariArr =
            newArray(serverObj.device, info, arrayData.data());
        resourceManager.registerArray((uint64_t)remoteObj.device,
            (uint64_t)remoteObj.object,
            anariArr,
            info);

        LOG(logging::Level::Info)
            << "Creating new array, objectID: " << remoteObj.object
            << ", ANARI handle: " << anariArr;
      } else if (message->type() == MessageType::SetParam) {
        CHECK(serverObj.device, "Error on anariSetParameter: invalid device");
        CHECK(serverObj.object, "Error on anariSetParameter: invalid object");

        std::string name;
        inputBuffer->read(name);

        ANARIDataType parmType;
        inputBuffer->read(parmType);

        if (anari::isObject(parmType)) {
          Handle hnd;
          inputBuffer->read((char *)&hnd, sizeof(hnd));

          const auto &registeredObjects =
              resourceManager.registeredObjects[(uint64_t)remoteObj.device];
          anariSetParameter(serverObj.device,
              serverObj.object,
              name.c_str(),
              parmType,
              &registeredObjects[hnd].object);

          LOG(logging::Level::Info)
              << "Set param \"" << name << "\" on object: " << remoteObj.object
              << ", param is an object. Handle: " << hnd
              << ", ANARI handle: " << registeredObjects[hnd].object;
        } else if (parmType == ANARI_STRING) {
          std::string parmValue;
          inputBuffer->read(parmValue);

          anariSetParameter(serverObj.device,
              serverObj.object,
              name.c_str(),
              parmType,
              parmValue.c_str());

          LOG(logging::Level::Info)
              << "Set param \"" << name << "\" on object: " << remoteObj.object;
        } else {
          std::vector<char> parmValue(anari::sizeOf(parmType));
          inputBuffer->read((char *)parmValue.data(), anari::sizeOf(parmType));

          anariSetParameter(serverObj.device,
              serverObj.object,
              name.c_str(),
              parmType,
              parmValue.data());

          LOG(logging::Level::Info)
              << "Set param \"" << name << "\" on object: " << remoteObj.object;
        }
      } else if (message->type() == MessageType::UnsetParam) {
        CHECK(serverObj.device, "Error on anariUnsetParameter: invalid device");
        CHECK(serverObj.object, "Error on anariUnsetParameter: invalid object");

        std::string name;
        inputBuffer->read(name);

        anariUnsetParameter(serverObj.device, serverObj.object, name.c_str());
      } else if (message->type() == MessageType::UnsetAllParams) {
        CHECK(serverObj.device,
            "Error on anariUnsetAllParameters: invalid device");
        CHECK(serverObj.device,
            "Error on anariUnsetAllParameters: invalid object");

        anariUnsetAllParameters(serverObj.device, serverObj.object);
      } else if (message->type() == MessageType::CommitParams) {
        CHECK(
            serverObj.device, "Error on anariCommitParameters: invalid device");
        CHECK(
            serverObj.object, "Error on anariCommitParameters: invalid object");

        anariCommitParameters(serverObj.device, serverObj.object);

        LOG(logging::Level::Info)
            << "Committed object. Handle: " << remoteObj.object;
      } else if (message->type() == MessageType::Release) {
        CHECK(serverObj.device, "Error on anariRelease: invalid device");
        CHECK(serverObj.object, "Error on anariRelease: invalid object");

        anariRelease(serverObj.device, serverObj.object);

        LOG(logging::Level::Info)
            << "Released object. Handle: " << remoteObj.object;
      } else if (message->type() == MessageType::Retain) {
        CHECK(serverObj.device, "Error on anariRetain: invalid device");
        CHECK(serverObj.object, "Error on anariRetain: invalid object");

        anariRetain(serverObj.device, serverObj.object);

        LOG(logging::Level::Info)
            << "Retained object. Handle: " << remoteObj.object;
      } else if (message->type() == MessageType::MapArray) {
        CHECK(serverObj.device, "Error on anariMapArray: invalid device");
        CHECK(serverObj.object, "Error on anariMapArray: invalid object");

        void *ptr =
            anariMapArray(serverObj.device, (ANARIArray)serverObj.object);

        const ArrayInfo &info = resourceManager.getArrayInfo(
            (Handle)remoteObj.device, (Handle)remoteObj.object);

        uint64_t numBytes = info.getSizeInBytes();

        auto outputBuffer = std::make_shared<Buffer>();
        outputBuffer->write(remoteObj.object);
        outputBuffer->write(numBytes);
        outputBuffer->write((const char *)ptr, numBytes);
        write(MessageType::ArrayMapped, outputBuffer);

        LOG(logging::Level::Info)
            << "Mapped array. Handle: " << remoteObj.object;
      } else if (message->type() == MessageType::UnmapArray) {
        CHECK(serverObj.device, "Error on anariUnmapArray: invalid device");
        CHECK(serverObj.object, "Error on anariUnmapArray: invalid object");

        // Array is currently mapped - unmap
        anariUnmapArray(serverObj.device, (ANARIArray)serverObj.object);

        // Now map so we can write to it
        void *ptr =
            anariMapArray(serverObj.device, (ANARIArray)serverObj.object);

        // Fetch data into separate buffer and copy
        std::vector<uint8_t> arrayData;
        if (inputBuffer->pos < message->size()) {
          ArrayInfo info = resourceManager.getArrayInfo(
              (Handle)remoteObj.device, (Handle)remoteObj.object);
          arrayData = translateArrayData(*inputBuffer, remoteObj.device, info);
          memcpy(ptr, arrayData.data(), arrayData.size());
        }

        // Unmap again..
        anariUnmapArray(serverObj.device, (ANARIArray)serverObj.object);

        auto outputBuffer = std::make_shared<Buffer>();
        outputBuffer->write(remoteObj.object);
        write(MessageType::ArrayUnmapped, outputBuffer);

        LOG(logging::Level::Info)
            << "Unmapped array. Handle: " << remoteObj.object;
      } else if (message->type() == MessageType::RenderFrame) {
        CHECK(serverObj.device, "Error on anariRenderFrame: invalid device");
        CHECK(serverObj.object, "Error on anariRenderFrame: invalid object");

        ANARIFrame frame = (ANARIFrame)serverObj.object;

        anariRenderFrame(serverObj.device, frame);

        // Block and send image over the wire
        anariFrameReady(serverObj.device, frame, ANARI_WAIT);

        CompressionFeatures cf = getCompressionFeatures();

        uint32_t width, height;
        ANARIDataType type;
        const char *color = (const char *)anariMapFrame(
            serverObj.device, frame, "channel.color", &width, &height, &type);
        size_t colorSize =
            type == ANARI_UNKNOWN ? 0 : width * height * anari::sizeOf(type);
        if (color != nullptr && colorSize != 0) {
          auto outputBuffer = std::make_shared<Buffer>();
          outputBuffer->write(remoteObj.object);
          outputBuffer->write(width);
          outputBuffer->write(height);
          outputBuffer->write(type);

          bool compressionTurboJPEG =
              cf.hasTurboJPEG && client.compression.hasTurboJPEG;

          if (compressionTurboJPEG
              && type == ANARI_UFIXED8_RGBA_SRGB) { // TODO: more formats..
            TurboJPEGOptions options;
            options.width = width;
            options.height = height;
            options.pixelFormat = TurboJPEGOptions::PixelFormat::RGBX;
            options.quality = 80;

            std::vector<uint8_t> compressed(
                getMaxCompressedBufferSizeTurboJPEG(options));

            if (compressed.size() != 0) {
              size_t compressedSize;
              if (compressTurboJPEG((const uint8_t *)color,
                      compressed.data(),
                      compressedSize,
                      options)) {
                uint32_t compressedSize32(compressedSize);
                outputBuffer->write(compressedSize32);
                outputBuffer->write(
                    (const char *)compressed.data(), compressedSize);

                LOG(logging::Level::Info) << "turbojpeg compression size: "
                                          << prettyBytes(compressedSize);
              }
            }
          } else {
            outputBuffer->write(color, colorSize);
          }
          write(MessageType::ChannelColor, outputBuffer);
        }

        const char *depth = (const char *)anariMapFrame(
            serverObj.device, frame, "channel.depth", &width, &height, &type);
        size_t depthSize =
            type == ANARI_UNKNOWN ? 0 : width * height * anari::sizeOf(type);
        if (depth != nullptr && depthSize != 0) {
          auto outputBuffer = std::make_shared<Buffer>();
          outputBuffer->write(remoteObj.object);
          outputBuffer->write(width);
          outputBuffer->write(height);
          outputBuffer->write(type);

          bool compressionSNAPPY = cf.hasSNAPPY && client.compression.hasSNAPPY;

          if (compressionSNAPPY && type == ANARI_FLOAT32) {
            SNAPPYOptions options;
            options.inputSize = depthSize;

            std::vector<uint8_t> compressed(
                getMaxCompressedBufferSizeSNAPPY(options));

            size_t compressedSize = 0;

            compressSNAPPY((const uint8_t *)depth,
                compressed.data(),
                compressedSize,
                options);

            uint32_t compressedSize32(compressedSize);
            outputBuffer->write(compressedSize32);
            outputBuffer->write(
                (const char *)compressed.data(), compressedSize);
          } else {
            outputBuffer->write(depth, depthSize);
          }
          write(MessageType::ChannelDepth, outputBuffer);
        }

        LOG(logging::Level::Info)
            << "Frame rendered. Object handle: " << remoteObj.object;
      } else if (message->type() == MessageType::FrameReady) {
        CHECK(serverObj.device, "Error on anariFrameReady: invalid device");
        CHECK(serverObj.object, "Error on anariFrameReady: invalid object");

        ANARIWaitMask waitMask;
        inputBuffer->read(waitMask);

        ANARIFrame frame = (ANARIFrame)serverObj.object;
        anariFrameReady(serverObj.device, frame, waitMask);

        auto outputBuffer = std::make_shared<Buffer>();
        outputBuffer->write(remoteObj.object);
        write(MessageType::FrameIsReady, outputBuffer);

        LOG(logging::Level::Info) << "Signal frame is ready to client";
      } else if (message->type() == MessageType::GetProperty) {
        CHECK(serverObj.device, "Error on anariGetProperty: invalid device");
        CHECK(serverObj.object, "Error on anariGetProperty: invalid object");

        std::string name;
        inputBuffer->read(name);

        ANARIDataType type;
        inputBuffer->read(type);

        uint64_t size;
        inputBuffer->read(size);

        ANARIWaitMask mask;
        inputBuffer->read(mask);

        auto outputBuffer = std::make_shared<Buffer>();

        if (type == ANARI_STRING_LIST) {
          const char *const *value = nullptr;
          int result = anariGetProperty(serverObj.device,
              serverObj.object,
              name.data(),
              type,
              &value,
              size,
              mask);

          outputBuffer->write(remoteObj.object);
          outputBuffer->write(name);
          outputBuffer->write(type);
          outputBuffer->write(size);
          outputBuffer->write(result);

          StringList stringList((const char **)value);
          outputBuffer->write(stringList);
        } else if (type == ANARI_DATA_TYPE_LIST) {
          throw std::runtime_error(
              "getProperty with ANARI_DATA_TYPE_LIST not implemented yet!");
        } else { // POD!
          std::vector<char> mem(size);

          int result = anariGetProperty(serverObj.device,
              serverObj.object,
              name.data(),
              type,
              mem.data(),
              size,
              mask);

          outputBuffer->write(remoteObj.object);
          outputBuffer->write(name);
          outputBuffer->write(type);
          outputBuffer->write(size);
          outputBuffer->write(result);
          outputBuffer->write((const char *)mem.data(), size);
        }
        write(MessageType::Property, outputBuffer);
      } else if (message->type() == MessageType::GetObjectSubtypes) {
        CHECK(serverObj.device,
            "Error on anariGetObjectSubtypes: invalid device");

        ANARIDataType objectType;
        inputBuffer->read(objectType);

        auto outputBuffer = std::make_shared<Buffer>();
        outputBuffer->write(objectType);

        const char **subtypes =
            anariGetObjectSubtypes(serverObj.device, objectType);

        StringList stringList(subtypes);
        outputBuffer->write(stringList);

        write(MessageType::ObjectSubtypes, outputBuffer);
      } else if (message->type() == MessageType::GetObjectInfo) {
        CHECK(serverObj.device, "Error on anariGetObjectInfo: invalid device");

        ANARIDataType objectType;
        inputBuffer->read(objectType);

        std::string objectSubtype;
        inputBuffer->read(objectSubtype);

        std::string infoName;
        inputBuffer->read(infoName);

        ANARIDataType infoType;
        inputBuffer->read(infoType);

        auto outputBuffer = std::make_shared<Buffer>();
        outputBuffer->write(objectType);
        outputBuffer->write(std::string(objectSubtype));
        outputBuffer->write(std::string(infoName));
        outputBuffer->write(infoType);

        const void *info = anariGetObjectInfo(serverObj.device,
            objectType,
            objectSubtype.data(),
            infoName.data(),
            infoType);

        if (info != nullptr) {
          if (infoType == ANARI_STRING) {
            auto *str = (const char *)info;
            outputBuffer->write(std::string(str));
          } else if (infoType == ANARI_STRING_LIST) {
            StringList stringList((const char **)info);
            outputBuffer->write(stringList);
          } else if (infoType == ANARI_PARAMETER_LIST) {
            ParameterList parameterList((const Parameter *)info);
            outputBuffer->write(parameterList);
          } else {
            outputBuffer->write((const char *)info, anari::sizeOf(infoType));
          }
        }
        write(MessageType::ObjectInfo, outputBuffer);
      } else if (message->type() == MessageType::GetParameterInfo) {
        CHECK(
            serverObj.device, "Error on anariGetParameterInfo: invalid device");

        ANARIDataType objectType;
        inputBuffer->read(objectType);

        std::string objectSubtype;
        inputBuffer->read(objectSubtype);

        std::string parameterName;
        inputBuffer->read(parameterName);

        ANARIDataType parameterType;
        inputBuffer->read(parameterType);

        std::string infoName;
        inputBuffer->read(infoName);

        ANARIDataType infoType;
        inputBuffer->read(infoType);

        auto outputBuffer = std::make_shared<Buffer>();
        outputBuffer->write(objectType);
        outputBuffer->write(objectSubtype);
        outputBuffer->write(parameterName);
        outputBuffer->write(parameterType);
        outputBuffer->write(infoName);
        outputBuffer->write(infoType);

        const void *info = anariGetParameterInfo(serverObj.device,
            objectType,
            objectSubtype.data(),
            parameterName.data(),
            parameterType,
            infoName.data(),
            infoType);

        if (info != nullptr) {
          if (infoType == ANARI_STRING) {
            auto *str = (const char *)info;
            outputBuffer->write(std::string(str));
          } else if (infoType == ANARI_STRING_LIST) {
            StringList stringList((const char **)info);
            outputBuffer->write(stringList);
          } else if (infoType == ANARI_PARAMETER_LIST) {
            ParameterList parameterList((const Parameter *)info);
            outputBuffer->write(parameterList);
          } else {
            outputBuffer->write((const char *)info, anari::sizeOf(infoType));
          }
        }
        write(MessageType::ParameterInfo, outputBuffer);
      } else {
        LOG(logging::Level::Warning)
            << "Unhandled message of size: " << message->size();
      }
    } else {
      // LOG(logging::Level::Info) << "written";
    }
  }
};

} // namespace remote

///////////////////////////////////////////////////////////////////////////////

static void printUsage()
{
  std::cout << "./anari-remote-server [{--help|-h}]\n"
            << "   [{--verbose|-v}]\n"
            << "   [{--library|-l} <ANARI library>]\n"
            << "   [{--port|-p} <N>]\n";
}

static void parseCommandLine(int argc, char *argv[])
{
  for (int i = 1; i < argc; i++) {
    std::string arg = argv[i];
    if (arg == "-v" || arg == "--verbose")
      g_verbose = true;
    if (arg == "--help" || arg == "-h") {
      printUsage();
      std::exit(0);
    } else if (arg == "-l" || arg == "--library")
      g_libraryType = argv[++i];
    else if (arg == "-p" || arg == "--port")
      g_port = std::stoi(argv[++i]);
  }
}

int main(int argc, char *argv[])
{
  parseCommandLine(argc, argv);
  remote::Server srv(g_port);
  srv.accept();
  srv.run();
  srv.wait();
}