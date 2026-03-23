#!/usr/bin/env python3
"""
Automatic ESP32-S3 Firmware Upload Script
Uploads compiled firmware to ESP32-S3 board automatically
"""

import os
import sys
import subprocess
import argparse
from pathlib import Path
import time


class FirmwareUploader:
    def __init__(self, project_root=None):
        """Initialize the uploader with project root directory"""
        if project_root is None:
            # Default to the tool directory where this script is located
            project_root = Path(__file__).parent
        self.project_root = Path(project_root)
        # Look for build files in the tool/build directory
        self.build_dir = self.project_root / "build"
        
        # Firmware file paths and their flash addresses (for ESP32-S3)
        # Note: bootloader is NOT flashed as it's already in the chip
        # littlefs.bin is commented out if it causes flash overflow issues
        self.firmware_files = {
            "partitions.bin": "0x8000",
            "firmware.bin": "0x10000",
            # "littlefs.bin": "0xD00000"  # Commented - causes overflow on 16MB flash
        }
    
    def check_requirements(self):
        """Check if required tools are installed"""
        missing = []
        
        # Check esptool
        try:
            import esptool
            print(f"✓ esptool.py found")
        except ImportError:
            print("✗ esptool.py not installed, installing...")
            try:
                subprocess.check_call([sys.executable, "-m", "pip", "install", "esptool"])
                print("✓ esptool.py installed successfully")
            except subprocess.CalledProcessError:
                missing.append("esptool")
        
        # Check pyserial
        try:
            import serial
            print(f"✓ pyserial found")
        except ImportError:
            print("✗ pyserial not installed, installing...")
            try:
                subprocess.check_call([sys.executable, "-m", "pip", "install", "pyserial"])
                print("✓ pyserial installed successfully")
            except subprocess.CalledProcessError:
                missing.append("pyserial")
        
        if missing:
            print(f"\n✗ Failed to install: {', '.join(missing)}")
            return False
        
        return True
    
    def find_serial_port(self):
        """Auto-detect ESP32 serial port"""
        try:
            from serial.tools import list_ports
        except ImportError:
            print("pyserial not available, installing...")
            try:
                subprocess.check_call([sys.executable, "-m", "pip", "install", "pyserial"])
                from serial.tools import list_ports
            except Exception as e:
                print(f"Failed to install pyserial: {e}")
                return None
        
        # Find all available ports
        ports = []
        for port in list_ports.comports():
            ports.append(port.device)
        
        if ports:
            # Prefer ports that might be ESP32 (common patterns)
            esp_patterns = ['ttyUSB', 'ttyACM', 'COM']
            for pattern in esp_patterns:
                for port_name in ports:
                    if pattern in port_name:
                        print(f"✓ Found ESP32 on port: {port_name}")
                        return port_name
            
            # If no pattern matches, use the first available port
            print(f"✓ Using port: {ports[0]}")
            return ports[0]
        
        print("✗ No serial ports found")
        return None
    
    def list_available_ports(self):
        """List all available serial ports"""
        try:
            from serial.tools import list_ports
        except ImportError:
            print("pyserial not installed, installing...")
            try:
                subprocess.check_call([sys.executable, "-m", "pip", "install", "pyserial"])
                from serial.tools import list_ports
            except Exception as e:
                print(f"Failed to install pyserial: {e}")
                return []
        
        ports = []
        print("\nAvailable serial ports:")
        for port in list_ports.comports():
            ports.append(port.device)
            print(f"  - {port.device:20} ({port.description})")
        
        if not ports:
            print("  (no ports found)")
        
        return ports
    
    def verify_firmware_files(self):
        """Verify that all required firmware files exist"""
        missing_files = []
        
        for filename in self.firmware_files.keys():
            filepath = self.build_dir / filename
            if not filepath.exists():
                missing_files.append(filename)
            else:
                size = filepath.stat().st_size
                print(f"✓ {filename:20} ({size:,} bytes)")
        
        if missing_files:
            print(f"\n✗ Missing files: {', '.join(missing_files)}")
            return False
        
        return True
    
    def upload(self, port=None, baud=921600):
        """Upload firmware to ESP32-S3"""
        
        print("\n" + "="*60)
        print("ESP32-S3 Firmware Upload")
        print("="*60 + "\n")
        
        # Check requirements
        if not self.check_requirements():
            return False
        
        # Verify firmware files exist
        print("\nChecking firmware files:")
        if not self.verify_firmware_files():
            print("\n✗ Build not found. Please compile firmware first with:")
            print("  platformio run --environment SignGlove")
            return False
        
        # Auto-detect port if not provided
        if port is None:
            print("\nDetecting serial port...")
            port = self.find_serial_port()
            if port is None:
                ports = self.list_available_ports()
                if not ports:
                    print("\nNo ESP32 found. Please connect the device and try again.")
                    return False
                print(f"\nPlease specify a port with --port argument")
                return False
        
        print(f"\nUsing port: {port}")
        print(f"Baud rate: {baud}")
        
        # Upload using esptool command line
        print("\nUploading firmware...\n")
        
        try:
            # Build esptool command with modern syntax (using hyphens)
            cmd = [
                "esptool.py",
                "--port", port,
                "--baud", str(baud),
                "--chip", "esp32s3",
                "--before", "default-reset",
                "--after", "hard-reset",
                "write-flash",
                "--flash-mode", "qio",
                "--flash-size", "16MB",
                "--flash-freq", "80m"
            ]
            
            # Add firmware files with their addresses
            for filename, address in self.firmware_files.items():
                filepath = self.build_dir / filename
                cmd.extend([address, str(filepath)])
            
            # Run esptool
            result = subprocess.run(cmd, capture_output=False, text=True)
            
            if result.returncode == 0:
                print("\n✓ Upload completed successfully!")
                print("Device rebooting...\n")
                return True
            else:
                print(f"\n✗ Upload failed with return code {result.returncode}")
                return False
            
        except FileNotFoundError:
            print("\n✗ esptool.py not found in PATH")
            print("Try reinstalling: pip install --upgrade esptool")
            return False
        except Exception as e:
            print(f"\n✗ Upload failed: {e}")
            return False
    



def main():
    parser = argparse.ArgumentParser(
        description="Automatically upload compiled ESP32-S3 firmware",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  python upload_firmware.py                    # Auto-detect port and upload (partitions + firmware)
  python upload_firmware.py --port /dev/ttyUSB0  # Upload to specific port
  python upload_firmware.py --list-ports       # List available ports
  python upload_firmware.py --baud 115200      # Upload with custom baud rate
  python upload_firmware.py --with-fs          # Include littlefs filesystem
        """
    )
    
    parser.add_argument(
        "--port",
        help="Serial port (auto-detect if not specified)"
    )
    parser.add_argument(
        "--baud",
        type=int,
        default=921600,
        help="Baud rate for upload (default: 921600)"
    )
    parser.add_argument(
        "--with-fs",
        action="store_true",
        help="Include littlefs.bin in upload (may cause overflow on 16MB flash)"
    )
    parser.add_argument(
        "--list-ports",
        action="store_true",
        help="List available serial ports and exit"
    )
    parser.add_argument(
        "--project-root",
        help="Project root directory (auto-detect if not specified)"
    )
    
    args = parser.parse_args()
    
    uploader = FirmwareUploader(args.project_root)
    
    # Add littlefs if requested
    if args.with_fs:
        uploader.firmware_files["littlefs.bin"] = "0xD00000"
    
    # List ports if requested
    if args.list_ports:
        uploader.list_available_ports()
        return 0
    
    # Upload firmware
    success = uploader.upload(port=args.port, baud=args.baud)
    return 0 if success else 1


if __name__ == "__main__":
    sys.exit(main())
