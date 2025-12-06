# FOS (FCIS Operating System)

Welcome to the FOS Project! This is a simple operating system kernel designed for educational purposes. This version includes significant improvements to the command-line interface and development environment.

<!-- TOC -->

- [1. Key Improvements & Differences](#1-key-improvements--differences)
- [2. Set up Environment](#2-set-up-environment)
  - [2.1. Windows - WSL (Recommended)](#21-windows---wsl-recommended)
  - [2.2. Linux](#22-linux)
- [3. Setup Workspace](#3-setup-workspace)
- [4. Debugging](#4-debugging)
- [5. Contributing](#5-contributing)
<!-- /TOC -->

## 1. Key Improvements & Differences

We have made substantial updates to the original codebase (first commit) to improve usability and stability:

### 🛠️ Interactive Command Line (Fixes & Features)

- **Destructive Backspace Fixed**: Resolved a critical bug where `Backspace` and `Delete` caused visual "ghosting" artifacts (characters removed from buffer but visible on screen). We implemented proper non-destructive cursor movement.
- **Insert Mode Implemented**: Added a modern text editing experience. You can now type in the middle of a command, and the text will shift right (instead of overwriting).
- **Correct Screen Redrawing**: The shell now correctly redraws the line upon edits, ensuring what you see matches what is executed.

### 💻 Development Environment

- **VSCode Integration**:
  - Replaced generic editor support with a dedicated `.vscode` configuration.
  - **IntelliSense**: Configured `c_cpp_properties.json` to resolve FOS kernel includes, removing spurious "red squiggles".
  - **Tasks**: Integrated `make qemu` and `make qemu-gdb` directly into VSCode Tasks.
- **Bootloader**: Streamlined boot process for QEMU.

---

## 2. Set up Environment

FOS is designed for a Linux-like environment.

### 2.1. Windows - WSL (Recommended)

We strongly recommend using **WSL2 (Windows Subsystem for Linux)**.

1.  **Install WSL**:
    Open PowerShell as Administrator and run:

    ```powershell
    wsl --install
    ```

    _Restart your computer if prompted._

2.  **Setup Ubuntu**:
    Open your Ubuntu terminal from the Start menu and proceed to the **Linux** steps below.

    _Note: To run graphical interfaces (like the QEMU window), ensure you are on Windows 11 or have a compatible X Server (like VcXsrv) installed on Windows 10._

### 2.2. Linux

You need the `build-essential` package, QEMU, and the 32-bit compilation toolchain.

1.  **Install Dependencies**:

    ```bash
    sudo apt-get update
    sudo apt-get install -y build-essential gdb git qemu-system-x86
    ```

2.  **Install Toolchain**:
    The project uses the `i386-elf-` prefix by default.

    _Option A: Use Standard GCC (Easier)_
    Install 32-bit support:

    ```bash
    sudo apt-get install -y gcc-multilib
    ```

    Then, you may need to edit `GNUmakefile` to remove the `i386-elf-` prefix, or override it when running make:

    ```bash
    make TOOLPREFIX= qemu
    ```

    _Option B: Install Cross-Compiler (Recommended for exactness)_
    Follow standard instructions to install `i386-elf-gcc` and add it to your PATH.

---

## 3. Setup Workspace

1.  **Install Visual Studio Code**: [Download Here](https://code.visualstudio.com/)
2.  **Open Project**:
    ```bash
    code .
    ```
3.  **Extensions**: Install the **C/C++** extension by Microsoft. The project includes a `.vscode/extensions.json` recommendation file (if present) or simply search for it.
4.  **Run**: Press `Ctrl + Shift + B` to select a build task (e.g., `Run QEMU`).

## 4. Debugging

1.  **Start Debug Mode**:
    Use the VSCode "Run and Debug" panel (or `F5` if configured), or run manually:

    ```bash
    make qemu-gdb
    ```

    This freezes QEMU and waits for a GDB connection.

2.  **Connect GDB**:
    Open another terminal:
    ```bash
    gdb
    ```
    (Or use the VSCode debugger if fully configured in `launch.json`).

## 5. Contributing

Contributions to improve the shell or kernel features are welcome.

1.  Fork the repository.
2.  Create a feature branch.
3.  Submit a Pull Request detailed your changes.
