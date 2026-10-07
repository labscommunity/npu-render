// Read-only Intel graphics telemetry. No clock/power/overclock setters are called.
#define NOMINMAX
#include <windows.h>
#include "igcl_api.h"
#include <chrono>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

static void item(std::ostream &out, const char *name, const ctl_oc_telemetry_item_t &v) {
    out << ',' << std::quoted(name) << ":{\"supported\":" << (v.bSupported ? "true" : "false")
        << ",\"units\":" << v.units << ",\"type\":" << v.type << ",\"value\":";
    if (!v.bSupported) out << "null";
    else switch (v.type) {
        case CTL_DATA_TYPE_INT8: out << int(v.value.data8); break;
        case CTL_DATA_TYPE_UINT8: out << unsigned(v.value.datau8); break;
        case CTL_DATA_TYPE_INT16: out << v.value.data16; break;
        case CTL_DATA_TYPE_UINT16: out << v.value.datau16; break;
        case CTL_DATA_TYPE_INT32: out << v.value.data32; break;
        case CTL_DATA_TYPE_UINT32: out << v.value.datau32; break;
        case CTL_DATA_TYPE_INT64: out << v.value.data64; break;
        case CTL_DATA_TYPE_UINT64: out << v.value.datau64; break;
        case CTL_DATA_TYPE_FLOAT:
            if (std::isfinite(v.value.datafloat)) out << v.value.datafloat; else out << "null";
            break;
        case CTL_DATA_TYPE_DOUBLE:
            if (std::isfinite(v.value.datadouble)) out << v.value.datadouble; else out << "null";
            break;
        default: out << "null";
    }
    out << '}';
}

template<typename T> static void metrics(std::ostream &out, const T &t) {
#define FIELD(name) item(out, #name, t.name)
    FIELD(timeStamp); FIELD(gpuEnergyCounter); FIELD(gpuCurrentClockFrequency);
    FIELD(gpuCurrentTemperature); FIELD(globalActivityCounter); FIELD(renderComputeActivityCounter);
    FIELD(mediaActivityCounter); FIELD(vramEnergyCounter); FIELD(vramCurrentClockFrequency);
    FIELD(vramReadBandwidthCounter); FIELD(vramWriteBandwidthCounter); FIELD(totalCardEnergyCounter);
    FIELD(gpuEffectiveClock); FIELD(vramReadBandwidth); FIELD(vramWriteBandwidth);
#undef FIELD
    out << ",\"gpuPowerLimited\":" << (t.gpuPowerLimited ? "true" : "false")
        << ",\"gpuTemperatureLimited\":" << (t.gpuTemperatureLimited ? "true" : "false")
        << ",\"gpuCurrentLimited\":" << (t.gpuCurrentLimited ? "true" : "false");
}

int main(int argc, char **argv) {
    ctl_api_handle_t api = nullptr;
    try {
        if (argc != 3) throw std::invalid_argument("usage: nr_gpu_power_probe SECONDS OUTPUT_JSONL");
        const int seconds = std::stoi(argv[1]);
        if (seconds < 1 || seconds > 1800) throw std::invalid_argument("seconds must be 1..1800");
        std::ofstream out(argv[2]);
        if (!out) throw std::runtime_error("cannot open telemetry report");
        out << std::setprecision(16);
        ctl_init_args_t args{};
        args.Size = sizeof(args); args.Version = 0;
        args.AppVersion = CTL_MAKE_VERSION(CTL_IMPL_MAJOR_VERSION, CTL_IMPL_MINOR_VERSION);
        args.flags = CTL_INIT_FLAG_USE_LEVEL_ZERO;
        auto status = ctlInit(&args, &api);
        out << "{\"kind\":\"init\",\"status\":" << uint32_t(status) << "}\n";
        if (status != CTL_RESULT_SUCCESS) throw std::runtime_error("IGCL initialization failed");
        uint32_t count = 0;
        status = ctlEnumerateDevices(api, &count, nullptr);
        if (status != CTL_RESULT_SUCCESS || !count) throw std::runtime_error("IGCL devices unavailable");
        std::vector<ctl_device_adapter_handle_t> devices(count);
        if (ctlEnumerateDevices(api, &count, devices.data()) != CTL_RESULT_SUCCESS)
            throw std::runtime_error("IGCL device enumeration failed");
        for (uint32_t i = 0; i < count; ++i) {
            LUID luid{};
            ctl_device_adapter_properties_t properties{};
            properties.Size = sizeof(properties); properties.Version = 2;
            properties.pDeviceID = &luid; properties.device_id_size = sizeof(luid);
            status = ctlGetDeviceProperties(devices[i], &properties);
            out << "{\"kind\":\"adapter\",\"index\":" << i << ",\"status\":" << uint32_t(status);
            if (status == CTL_RESULT_SUCCESS)
                out << ",\"name\":" << std::quoted(properties.name) << ",\"luid_high\":"
                    << uint32_t(luid.HighPart) << ",\"luid_low\":" << luid.LowPart;
            out << "}\n";
        }
        for (int sample = 0; sample < seconds * 2; ++sample) {
            for (uint32_t i = 0; i < count; ++i) {
                const double now = std::chrono::duration<double>(
                    std::chrono::system_clock::now().time_since_epoch()).count();
                ctl_power_telemetry_v2_t telemetry{};
                telemetry.Size = sizeof(telemetry); telemetry.Version = 1;
                status = ctlPowerTelemetryGetV2(devices[i], &telemetry);
                out << "{\"kind\":\"sample\",\"index\":" << i << ",\"unix_s\":" << now
                    << ",\"v2_status\":" << uint32_t(status);
                if (status == CTL_RESULT_SUCCESS) metrics(out, telemetry);
                else {
                    ctl_power_telemetry_t legacy{};
                    legacy.Size = sizeof(legacy); legacy.Version = 1;
                    status = ctlPowerTelemetryGet(devices[i], &legacy);
                    out << ",\"v1_status\":" << uint32_t(status);
                    if (status == CTL_RESULT_SUCCESS) metrics(out, legacy);
                }
                out << "}\n";
            }
            out.flush();
            if (!out) throw std::runtime_error("telemetry write failed");
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }
        ctlClose(api);
        return 0;
    } catch (const std::exception &error) {
        if (api) ctlClose(api);
        std::cerr << error.what() << '\n';
        return 1;
    }
}
