
- Make `videoCaptureDeviceManager()` return the PipeWireCaptureDeviceManager
- Implement a new pipewire device provider that wraps `pipewiredeviceprovider` and sets the FD retrieved from Camera portal on it (see snapshot/aperture/src/device_provider.rs)
- Use gst device monitor only if Camera portal not available?
