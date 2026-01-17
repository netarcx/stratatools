"""
Cartridge Data Model

Qt-friendly wrapper around Cartridge protobuf for GUI operations.
"""

from datetime import datetime
from PyQt5.QtCore import QObject, pyqtSignal
from stratatools import cartridge_pb2


class CartridgeModel(QObject):
    """
    Data model wrapper for Cartridge protobuf.

    Provides methods to convert between protobuf format and Python
    dictionaries, handle timestamps, and emit signals on data changes.
    """

    # Signal emitted when cartridge data changes
    data_changed = pyqtSignal()

    def __init__(self, cartridge=None):
        """
        Initialize model with optional cartridge.

        Args:
            cartridge: Cartridge protobuf object or None for new cartridge
        """
        super().__init__()
        self.cartridge = cartridge if cartridge else cartridge_pb2.Cartridge()

    def to_dict(self):
        """
        Convert cartridge to dictionary for display/editing.

        Returns:
            dict: Cartridge data with human-readable values
        """
        c = self.cartridge

        # Convert timestamps to datetime objects
        mfg_date = None
        if c.HasField("manufacturing_date"):
            mfg_date = c.manufacturing_date.ToDatetime()

        use_date = None
        if c.HasField("last_use_date"):
            use_date = c.last_use_date.ToDatetime()

        return {
            "serial_number": c.serial_number,
            "material_name": c.material_name,
            "manufacturing_lot": c.manufacturing_lot,
            "manufacturing_date": mfg_date,
            "last_use_date": use_date,
            "initial_material_quantity": c.initial_material_quantity,
            "current_material_quantity": c.current_material_quantity,
            "key_fragment": c.key_fragment.hex() if c.key_fragment else "",
            "version": c.version,
            "signature": c.signature,
        }

    def from_dict(self, data):
        """
        Update cartridge from dictionary.

        Args:
            data (dict): Dictionary with cartridge fields
        """
        c = self.cartridge

        # Simple fields
        if "serial_number" in data:
            c.serial_number = float(data["serial_number"])

        if "material_name" in data:
            c.material_name = str(data["material_name"])

        if "manufacturing_lot" in data:
            c.manufacturing_lot = str(data["manufacturing_lot"])

        if "initial_material_quantity" in data:
            c.initial_material_quantity = float(data["initial_material_quantity"])

        if "current_material_quantity" in data:
            c.current_material_quantity = float(data["current_material_quantity"])

        if "version" in data:
            c.version = int(data["version"])

        if "signature" in data:
            sig = str(data["signature"])
            c.signature = sig[:9]  # Limit to 9 characters

        # Timestamps
        if "manufacturing_date" in data and data["manufacturing_date"]:
            if isinstance(data["manufacturing_date"], datetime):
                c.manufacturing_date.FromDatetime(data["manufacturing_date"])

        if "last_use_date" in data and data["last_use_date"]:
            if isinstance(data["last_use_date"], datetime):
                c.last_use_date.FromDatetime(data["last_use_date"])

        # Key fragment (convert from hex string if needed)
        if "key_fragment" in data:
            key_frag = data["key_fragment"]
            if isinstance(key_frag, str):
                # Remove spaces and convert from hex
                key_frag = key_frag.replace(" ", "").replace(":", "")
                try:
                    c.key_fragment = bytes.fromhex(key_frag)
                except ValueError:
                    pass  # Invalid hex string, ignore
            elif isinstance(key_frag, bytes):
                c.key_fragment = key_frag

        self.data_changed.emit()

    def validate(self):
        """
        Validate cartridge data.

        Returns:
            list[str]: List of validation error messages (empty if valid)
        """
        errors = []
        c = self.cartridge

        # Serial number
        if c.serial_number <= 0:
            errors.append("Serial number must be positive")

        # Material name
        if not c.material_name:
            errors.append("Material name is required")

        # Material quantities
        if c.initial_material_quantity < 0:
            errors.append("Initial material quantity cannot be negative")

        if c.current_material_quantity < 0:
            errors.append("Current material quantity cannot be negative")

        if c.current_material_quantity > c.initial_material_quantity:
            errors.append("Current material quantity cannot exceed initial quantity")

        # Version
        if c.version < 0 or c.version > 65535:
            errors.append("Version must be between 0 and 65535")

        # Signature
        if len(c.signature) > 9:
            errors.append("Signature must be 9 characters or less")

        # Key fragment
        if len(c.key_fragment) != 8:
            errors.append("Key fragment must be exactly 8 bytes")

        return errors

    def get_remaining_percent(self):
        """
        Calculate remaining material as percentage.

        Returns:
            float: Percentage remaining (0-100)
        """
        if self.cartridge.initial_material_quantity == 0:
            return 0.0

        percent = (self.cartridge.current_material_quantity /
                   self.cartridge.initial_material_quantity * 100.0)
        return min(100.0, max(0.0, percent))

    def is_empty(self):
        """Check if cartridge is empty"""
        return self.cartridge.current_material_quantity <= 0

    def is_full(self):
        """Check if cartridge is full"""
        return (self.cartridge.current_material_quantity >=
                self.cartridge.initial_material_quantity)

    def refill(self):
        """
        Refill cartridge to initial quantity and update dates.

        Sets current quantity to initial quantity and updates
        both manufacturing and last use dates to current time.
        """
        c = self.cartridge
        c.current_material_quantity = c.initial_material_quantity

        now = datetime.now()
        c.manufacturing_date.FromDatetime(now)
        c.last_use_date.FromDatetime(now)

        self.data_changed.emit()
