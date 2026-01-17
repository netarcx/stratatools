"""
Serial Port Scanner

Auto-detect serial ports and identify ESP32 devices.
"""

import serial.tools.list_ports


class SerialPortScanner:
    """
    Utility class for scanning and identifying serial ports.

    Provides methods to detect available serial ports and identify
    ESP32 devices based on VID/PID or manufacturer information.
    """

    # Known ESP32 USB vendor/product IDs
    ESP32_VIDS = [
        0x10C4,  # Silicon Labs CP210x (common on ESP32 boards)
        0x1A86,  # QinHeng Electronics CH340/CH341
        0x0403,  # FTDI
        0x303A,  # Espressif Systems (native USB ESP32-S2/S3/C3)
    ]

    @staticmethod
    def scan_ports():
        """
        Scan for all available serial ports.

        Returns:
            list[dict]: List of port information dictionaries with keys:
                - port: Port device name (e.g., "/dev/ttyUSB0", "COM3")
                - description: Human-readable description
                - hwid: Hardware ID string
                - manufacturer: Manufacturer name (if available)
                - is_esp32: Boolean indicating if likely an ESP32
        """
        ports = []

        for port_info in serial.tools.list_ports.comports():
            port_data = {
                "port": port_info.device,
                "description": port_info.description or "Unknown Device",
                "hwid": port_info.hwid or "",
                "manufacturer": port_info.manufacturer or "",
                "is_esp32": SerialPortScanner.is_esp32(port_info)
            }
            ports.append(port_data)

        # Sort by port name
        ports.sort(key=lambda x: x["port"])

        return ports

    @staticmethod
    def is_esp32(port_info):
        """
        Check if a port is likely an ESP32 device.

        Args:
            port_info: pyserial ListPortInfo object

        Returns:
            bool: True if the port appears to be an ESP32
        """
        # Check VID (Vendor ID)
        if hasattr(port_info, 'vid') and port_info.vid in SerialPortScanner.ESP32_VIDS:
            return True

        # Check manufacturer string
        if port_info.manufacturer:
            manufacturer_lower = port_info.manufacturer.lower()
            if any(keyword in manufacturer_lower for keyword in
                   ['silicon labs', 'espressif', 'qinheng', 'ftdi', 'ch340', 'cp210']):
                return True

        # Check description
        if port_info.description:
            desc_lower = port_info.description.lower()
            if any(keyword in desc_lower for keyword in
                   ['esp32', 'silicon labs', 'cp210', 'ch340', 'ch341']):
                return True

        return False

    @staticmethod
    def get_esp32_ports():
        """
        Get a list of ports that are likely ESP32 devices.

        Returns:
            list[str]: List of port device names (e.g., ["/dev/ttyUSB0", "COM3"])
        """
        all_ports = SerialPortScanner.scan_ports()
        esp32_ports = [port["port"] for port in all_ports if port["is_esp32"]]
        return esp32_ports

    @staticmethod
    def get_port_display_name(port_data):
        """
        Generate a user-friendly display name for a port.

        Args:
            port_data (dict): Port information dictionary from scan_ports()

        Returns:
            str: Display name like "COM3 - ESP32 (Silicon Labs)"
        """
        port = port_data["port"]
        desc = port_data["description"]

        # Create readable display name
        if port_data["is_esp32"]:
            return f"{port} - {desc} (ESP32)"
        else:
            return f"{port} - {desc}"
