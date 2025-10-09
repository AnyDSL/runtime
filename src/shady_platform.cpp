#include "shady_platform.h"


using namespace shady;

struct ShadyBuffer {
    ShadyPlatform::ShadyDevice& device_;
    shady::Buffer* handle_;
    size_t size_;

    ShadyBuffer(ShadyPlatform::ShadyDevice& device, size_t size);
};

struct ShadyProgram {
    ShadyPlatform::ShadyDevice& device_;
    shady::Module* module_;
    shady::Program* handle_;

    ShadyProgram(ShadyPlatform::ShadyDevice&, std::string);
};

struct ShadyPlatform::ShadyDevice {
    ShadyPlatform& platform_;
    DeviceId id_;
    shady::Device* handle_;
    shady::TargetConfig target_config_;

    std::unordered_map<uint64_t, std::unique_ptr<ShadyBuffer>> buffers_;
    std::unordered_map<std::string, std::unique_ptr<ShadyProgram>> programs_;

    ShadyDevice(ShadyPlatform& platform, DeviceId id) : platform_(platform), id_(id) {
        handle_ = shd_rn_get_device(platform.runner_, id);
        target_config_ = shd_rn_get_device_target_config(&platform_.compiler_config_, handle_);
    }

    ShadyProgram& load_program(std::string filename);
};

ShadyBuffer::ShadyBuffer(ShadyPlatform::ShadyDevice& device, size_t size) : device_(device), size_(size) {
    handle_ = shady::shd_rn_allocate_buffer_device(device_.handle_, size);
}

ShadyPlatform::ShadyPlatform(Runtime *r) : Platform(r) {
    shady::RunnerConfig cfg;
    cfg.dump_spv = true;
    cfg.use_validation = true;

    compiler_config_.dynamic_scheduling = false;

    runner_ = shady::shd_rn_initialize(cfg);
    for (size_t i = 0; i < shd_rn_device_count(runner_); i++) {
        devices_.emplace_back(std::make_unique<ShadyDevice>(*this, (DeviceId) i));
    }
}

ShadyPlatform::~ShadyPlatform() {
    shd_rn_shutdown(runner_);
}

void* ShadyPlatform::alloc(DeviceId dev, int64_t size) {
    auto& device = devices_[dev];
    auto buffer = std::make_unique<ShadyBuffer>(*device, (size_t) size);
    uint64_t device_address = shd_rn_get_buffer_device_pointer(buffer->handle_);
    device->buffers_[device_address] = std::move(buffer);
    return reinterpret_cast<void*>(device_address);
}

void* ShadyPlatform::alloc_host(DeviceId dev, int64_t size) {
    assert(false);
}

void* ShadyPlatform::alloc_unified(DeviceId dev, int64_t size) {
    assert(false);
}

void* ShadyPlatform::get_device_ptr(DeviceId dev, void *ptr) {
    assert(false);
}

void ShadyPlatform::release(DeviceId dev, void *ptr) {
    auto& device = devices_[dev];
    device->buffers_.erase((uint64_t) ptr);
}

void ShadyPlatform::release_host(DeviceId dev, void *ptr) {
    assert(false);
}

ShadyProgram& ShadyPlatform::ShadyDevice::load_program(std::string filename) {
    if (auto found = programs_.find(filename); found != programs_.end())
        return *found->second;
    return *(programs_[filename] = std::make_unique<ShadyProgram>(*this, filename));
}

ShadyProgram::ShadyProgram(ShadyPlatform::ShadyDevice& device, std::string file_name) : device_(device) {
    std::string program_src = device_.platform_.runtime_->load_file(file_name);
    shd_driver_load_source_file(&device_.platform_.compiler_config_, &device_.target_config_, SrcSPIRV, program_src.size(), program_src.c_str(), "test", &module_);
    handle_ = shd_rn_new_program_from_module(device_.platform_.runner_, &device_.platform_.compiler_config_, module_);
}

void ShadyPlatform::launch_kernel(DeviceId dev, const LaunchParams &launch_params) {
    auto& device = devices_[dev];
    auto& program = device->load_program(launch_params.file_name);

    std::vector<void*> args;
    for (uint32_t argIdx = 0; argIdx < launch_params.num_args; ++argIdx) {
        args.push_back(launch_params.args.data[argIdx]);
        //WRAP_LEVEL_ZERO(zeKernelSetArgumentValue(hKernel, argIdx, launch_params.args.sizes[argIdx], launch_params.args.data[argIdx]));
    }

    shady::Command* d = shady::shd_rn_launch_kernel(program.handle_, device->handle_, launch_params.kernel_name, launch_params.grid[0] / launch_params.block[0], launch_params.grid[1] / launch_params.block[1], launch_params.grid[2] / launch_params.block[2], args.size(), args.data(), nullptr);
    assert(d);
    shady::shd_rn_wait_completion(d);
}

void ShadyPlatform::synchronize(DeviceId dev) {}

void ShadyPlatform::copy(DeviceId dev_src, const void *src, int64_t offset_src, DeviceId dev_dst, void *dst, int64_t offset_dst, int64_t size) {
    assert(false);
}

void ShadyPlatform::copy_from_host(const void *src, int64_t offset_src, DeviceId dev_dst, void *dst, int64_t offset_dst, int64_t size) {
    auto& dst_device = devices_[dev_dst];
    auto& dst_buffer = dst_device->buffers_[(uint64_t) dst];
    shd_rn_copy_to_buffer(dst_buffer->handle_, offset_dst, (char*) src + offset_src, size);
}

void ShadyPlatform::copy_to_host(DeviceId dev_src, const void *src, int64_t offset_src, void *dst, int64_t offset_dst, int64_t size) {
    auto& src_device = devices_[dev_src];
    auto& src_buffer = src_device->buffers_[(uint64_t) src];
    shd_rn_copy_from_buffer(src_buffer->handle_, offset_src, (char*) dst + offset_dst, size);
}

void register_shady_platform(Runtime* runtime) {
    runtime->register_platform<ShadyPlatform>();
}