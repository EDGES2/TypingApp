## 🎹 Shortcuts Guide

### 🖥 MacOS

- `esc` — **Close app**
- `option` + `delete` — **Delete whole word**
- `⌘ (Left CMD)` + `⌘ (Right CMD)` — **Pause/Unpause**
- `s` — **Show stats** *(only when paused)*
- `t` — **Open `text.txt`** *(only when paused)*

### 🖥 Windows

- `esc` — **Close app**
- `Ctrl` + `backspace` — **Delete whole word**
- `Left alt` + `Right alt` — **Pause/Unpause**
- `s` — **Show stats** *(only when paused)*
- `t` — **Open `text.txt`** *(only when paused)*

---

## 🛠 Compilation and Setup Guide

### 🖥 macOS

This section provides instructions for compiling the TypingApp project on macOS.

### 1. Prerequisites (macOS)

* **CMake**: Version 3.20 or higher recommended.
* **A C compiler**: Supporting C23 standard (e.g., GCC, Clang) **CLion recommended**.
* **Homebrew**: A package manager for macOS. If you don't have it, install it from [https://brew.sh/](https://brew.sh/).

### 2. Installing Libraries (macOS)

Install the SDL2 and SDL2_ttf libraries using Homebrew:
```bash
brew install sdl2 sdl2_ttf
  ```

### 3. Compiling the Project (macOS)

1.  Navigate to the project's root directory.
2. Create a build directory:
    ```bash
    mkdir build
    cd build
    ```

3. Configure CMake with Homebrew prefix: When configuring your CMake project, you should set CMAKE_PREFIX_PATH to point to your Homebrew installation.
* **In CLion (or similar IDE):** Go to `Settings/Preferences -> Build, Execution, Deployment -> CMake`. In the "CMake options" field, add the following:
  ```
  -DCMAKE_PREFIX_PATH="C:/dev_libs/SDL2-2.32.6/x86_64-w64-mingw32;C:/dev_libs/SDL2_ttf-2.24.0/x86_64-w64-mingw32"
  ```
* **From the command line**:
    ```bash
    cmake .. -DCMAKE_PREFIX_PATH=/opt/homebrew
    ```
(Note: /opt/homebrew is the default prefix for Homebrew on Apple Silicon. For Intel Macs, it's often /usr/local. Adjust if necessary.)


### 4. Compile the project: After successful configuration, build the project.
```bash
    cmake --build .
```
The executable will be generated within the build directory.

### 🖥 Windows

This guide provides instructions for compiling the TypingApp project on your own Windows PC, including setting up necessary development libraries and optionally creating an installer.

### 1. Prerequisites (Windows)

* **CMake**: Version 3.20 or higher recommended.
* **A C compiler**: Supporting C23 standard (e.g., GCC, Clang). **CLion recommended**

### 2. Setting Up Development Libraries (Windows)

To compile the application, you'll need the SDL2 and SDL2_ttf development libraries. These are provided in the `iwant_to_compile_on_my_own_pc.zip` archive.

1.  **Extract `iwant_to_compile_on_my_own_pc.zip`**:
    * Unzip the `iwant_to_compile_on_my_own_pc.zip` file.
    * You will find two folders inside: `dev_libs` and `MSYS2_Setup`.
    * Place **both** `dev_libs` and `MSYS2_Setup` folders directly into your `C:\` drive. Your paths should look like `C:\dev_libs\...` and `C:\MSYS2_Setup\...`.

### 3. Compiling the Project (Windows)

1.  **Navigate to the project's root directory**.
2.  **Create a build directory** inside project folder **TypingApp/** (in CLion just reload CMake Project):
3.  **Configure CMake with library paths**:
    When configuring your CMake project (e.g., in CLion's CMake options or via the command line), you **must** specify the paths to the SDL2 and SDL2_ttf libraries using the `CMAKE_PREFIX_PATH` variable.
    * **In CLion (or similar IDE):** Go to `Settings/Preferences -> Build, Execution, Deployment -> CMake`. In the "CMake options" field, add the following:
        ```
        -DCMAKE_PREFIX_PATH="C:/dev_libs/SDL2-2.32.6/x86_64-w64-mingw32;C:/dev_libs/SDL2_ttf-2.24.0/x86_64-w64-mingw32"
        ```
    * **From the command line:** When you run `cmake`, include the option:
        ```bash
        cmake .. -G "MinGW Makefiles" -DCMAKE_PREFIX_PATH="C:/dev_libs/SDL2-2.32.6/x86_64-w64-mingw32;C:/dev_libs/SDL2_ttf-2.24.0/x86_64-w64-mingw32"
        ```
      *(Note: The `MinGW Makefiles` generator is common for Windows with GCC/MinGW)*
4.  **Compile the project**:
    After successful configuration, build the project.
    * **In CLion:** Click the "Build All" button.
    * **From the command line:**
        ```bash
        cmake --build .
        ```
    The executable will typically be found in `build/src/` or directly in the `build` directory, alongside the necessary DLLs copied by the build process.

### 4. Creating an Installer (Optional, Windows)

If you wish to create an installer for the application, you will need NSIS (Nullsoft Scriptable Install System).

1.  **Install NSIS**:
    * Unzip the `NSIS.zip` archive.
    * Place the extracted `NSIS` folder directly into `C:\Program Files (x86)\`. Your path should look like `C:\Program Files (x86)\NSIS\`.
2.  **Add NSIS to System Path (Optional, Recommended)**:
    Adding NSIS to your system's PATH environment variable allows you to run `cpack.exe` from any directory without specifying its full path.
    * Search for "Environment Variables" in the Windows search bar and select "Edit the system environment variables".
    * Click "Environment Variables..." in the System Properties window.
    * Under "System variables", find and select the `Path` variable, then click "Edit...".
    * Click "New" and add the path to your NSIS installation's `bin` directory (e.g., `C:\Program Files (x86)\NSIS\bin`).
    * Click "OK" on all open windows to save the changes. You might need to restart your command prompt/PowerShell for the changes to take effect.
3.  **Generate the installer**:
    * Open PowerShell (or Command Prompt).
    * Navigate to your CMake build directory (e.g., `C:\path\to\TypingApp\cmake-build-debug`).
    * Execute the CPack command:
        ```powershell
        & "D:\Clion\CLion 2025.1.1\bin\cmake\win\x64\bin\cpack.exe" -G NSIS
        ```
      *(Adjust the path to `cpack.exe` based on your CLion installation or if you have CMake installed globally. If CMake is in your system PATH after step 2, you might just be able to run `cpack.exe -G NSIS`)*.
    * A `.exe` installer file will be generated in your build directory.

