<h1>Keep Windows bluetooth A2DP alive by playing silent audio</h1>


***100% AI CODE. I DO NOT KNOW Cpp AT ALL.***

This program keeps Bluetooth A2DP devices alive by playing a silent audio loop.


<h2>Features</h2>

- Detect device change (online/offline) through **IMMDeviceEnumerator** and restart silence playback if needed
- Blocklist for devices that you do not want to occupy
- Log write/display, or just completely silent

<h2>Usage</h2>

**Command line options:**

- ``-c``, ``--console``: Output logs to the console.
- ``-v``, ``--verbose``: Write logs to disk. Logs are saved with timestamped filenames (YYYYMMDD_HHMMSS).
- Both options can be used simultaneously. Default behavior without parameters is silent run (no console, no log file).

**Device Block:**
For certain devices you don't want to occupy (For usage like ASIO etc.), add the device name to **blocked_devices.txt**. 1 device name per line. E.g. if you have a headphone which name is **ABCDEF**, then add a line only contains **ABCDEF** into that file. No need to include the full device type like **Headphones (ABCDEF)**. 

**Startup:** To start on boot, add a shortcut to ``shell:startup``.

<h2>Compilation (MSVC required):</h2>

**Compile command:**

for old_keepalive.cpp (***DEPRICATED DO NOT USE THIS VERSION UNLESS YOU WANT TO PLAY WITH MY GARBAGE CODE***) 

``cl keepalive.cpp /Fe:keepalive.exe /std:c++17 /EHsc ole32.lib propsys.lib winmm.lib user32.lib uuid.lib /link /SUBSYSTEM:WINDOWS``

for keepalive_log.cpp (**Current Version**): 

``cl keepalive_log.cpp /Fe:keepalive_log.exe /std:c++17 /EHsc ole32.lib propsys.lib winmm.lib user32.lib uuid.lib /link /SUBSYSTEM:WINDOWS``


