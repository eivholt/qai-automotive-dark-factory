// Read-only platform inventory; no model, power setting or device config changes.
// Build with matching QAIRT include/QNN headers and link with -ldl.
#include <QnnInterface.h>
#include <HTP/QnnHtpDevice.h>
#include <dlfcn.h>
#include <iostream>
#include <sstream>

int main(int argc,char** argv) {
    if (argc!=2) { std::cerr<<"Usage: probe_qnn_devices /path/libQnnHtp.so\n"; return 2; }
    void* lib=dlopen(argv[1],RTLD_NOW|RTLD_LOCAL);
    if (!lib) { std::cerr<<dlerror()<<'\n'; return 1; }
    auto providers=reinterpret_cast<decltype(&QnnInterface_getProviders)>(dlsym(lib,"QnnInterface_getProviders"));
    const QnnInterface_t** list=nullptr; uint32_t count=0;
    if (!providers || providers(&list,&count)!=QNN_SUCCESS || !list) {
        std::cerr<<"Cannot enumerate QNN providers\n"; dlclose(lib); return 1;
    }
    for (uint32_t p=0;p<count;++p) {
        if (!list[p]) continue;
        const auto& version=list[p]->apiVersion.coreApiVersion;
        if (version.major!=QNN_API_VERSION_MAJOR || version.minor<QNN_API_VERSION_MINOR) continue;
        auto api=list[p]->QNN_INTERFACE_VER_NAME;
        if (!api.deviceGetPlatformInfo || !api.deviceFreePlatformInfo) continue;
        const QnnDevice_PlatformInfo_t* platform=nullptr;
        auto error=api.deviceGetPlatformInfo(nullptr,&platform);
        if (error!=QNN_SUCCESS || !platform || platform->version!=QNN_DEVICE_PLATFORM_INFO_VERSION_1) {
            std::cerr<<"deviceGetPlatformInfo failed: "<<error<<'\n';
            if (platform) api.deviceFreePlatformInfo(nullptr,platform);
            dlclose(lib); return 1;
        }
        std::ostringstream output;
        auto invalid = [&]() {
            std::cerr<<"Unsupported or invalid QNN platform data\n";
            api.deviceFreePlatformInfo(nullptr,platform); dlclose(lib); return 1;
        };
        if (platform->v1.numHwDevices && !platform->v1.hwDevices) return invalid();
        output<<"{\"schema\":\"qnn-platform-inventory-v1\",\"api\":["<<version.major<<','<<version.minor
            <<','<<version.patch<<"],\"devices\":[";
        for (uint32_t d=0;d<platform->v1.numHwDevices;++d) {
            const auto& hardware=platform->v1.hwDevices[d];
            if (hardware.version!=QNN_DEVICE_HARDWARE_DEVICE_INFO_VERSION_1) return invalid();
            const auto& device=hardware.v1;
            if (device.numCores && !device.cores) return invalid();
            if (d) output<<',';
            output<<"{\"device_id\":"<<device.deviceId<<",\"device_type\":"<<device.deviceType
                <<",\"cores\":[";
            for (uint32_t c=0;c<device.numCores;++c) {
                if (device.cores[c].version!=QNN_DEVICE_CORE_INFO_VERSION_1) return invalid();
                if (c) output<<',';
                output<<"{\"core_id\":"<<device.cores[c].v1.coreId<<",\"core_type\":"<<device.cores[c].v1.coreType<<'}';
            }
            auto* ext=device.deviceInfoExtension;
            output<<']';
            if (ext && ext->devType==QNN_HTP_DEVICE_TYPE_ON_CHIP)
                output<<",\"soc_model\":"<<ext->onChipDevice.socModel<<",\"arch\":"<<ext->onChipDevice.arch;
            output<<'}';
        }
        output<<"]}\n";
        error=api.deviceFreePlatformInfo(nullptr,platform);
        dlclose(lib);
        if (error==QNN_SUCCESS) std::cout<<output.str();
        return error==QNN_SUCCESS ? 0:1;
    }
    std::cerr<<"No compatible QNN interface\n"; dlclose(lib); return 1;
}
