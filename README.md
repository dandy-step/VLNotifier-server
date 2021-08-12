## VLNotifier-Server

C++ Server application for VLNotifier Android app that detects live casters and sends a notification to the Android app on the target device. Stores all info on a local SQLite database for caching and filtering. Able to customize notifications, set timeouts, handle redirects and detect individual casting sessions.

### HOW TO BUILD:
Includes native GCC solution, run build script from your terminal to generate vlnotifier.elf

### HOW TO USE:
Execute vlnotifier.elf from terminal, available arguments:

**"release"** - runs in release mode, meaning that notifications will be generated and sent to all devices

**"force"** - send out test notification

**"-i"** - runs in interactive notification mode, allowing you to generate your own notification on the terminal and send it out to select channels

**"-q"** - manually query the caster database by entering SQL command on the terminal
