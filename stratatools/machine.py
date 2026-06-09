#
# See the LICENSE file
#

import binascii

#
# A machine is a printer from stratasys
#
type_to_number = {
    "fox": "2C30478BB7DE81E8",
    "fox2": "2C30479BB7DE81E8",
    "ktype": "6B2A268B5ED3374A",
    "prodigy": "5394D7657CED641D",
    "quantum": "76C454D532E610F7",
    "uprint": "F3A91DBE6B0B2255",
    "uprintse": "09FBD4B61FC0B327",
}

number_to_type = {}

def get_machine_types():
    return type_to_number.keys()

def get_number_from_type(type):
    return bytes.fromhex(type_to_number[type])

def get_type_from_number(number):
    if len(number_to_type) == 0:
        for type_name, number_hex in type_to_number.items():
            number_to_type[number_hex] = type_name

    return number_to_type[binascii.hexlify(number).decode().upper()]
