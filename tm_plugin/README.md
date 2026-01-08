# TMPluginRyzenSDK

TrafficMonitor plugin that reads Ryzen temperature, usage, and power via the Ryzen SDK.

Build
- `msbuild tm_plugin\TMPluginRyzenSDK.vcxproj /p:Configuration=Release /p:Platform=x64`
- Or open `RyzenMasterMonitor.sln` and build the `TMPluginRyzenSDK` project.

Deploy
- Copy `bin\RyzenTMPlugin.dll` to `TrafficMonitor\plugins\`.
- Place the SDK runtime files next to the plugin:
  - `TrafficMonitor\plugins\RyzenSDK\bin\Platform.dll`
  - `TrafficMonitor\plugins\RyzenSDK\bin\Device.dll`
  - `TrafficMonitor\plugins\RyzenSDK\bin\AMDRyzenMasterDriver.sys`
- Run TrafficMonitor as administrator the first time so the driver can be installed.

SDK path resolution
The plugin tries these locations in order:
- `plugins\RyzenSDK` (if `bin\Platform.dll` exists)
- `plugins` (if `bin\Platform.dll` exists)
- `AMDRMSDKPATH`
- `AMDRMMONITORSDKPATH`

If the driver service name changes in a newer SDK, update `RM_DRIVER_NAME` in `inc\Utility.hpp`.
