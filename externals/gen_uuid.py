import uuid

def generate_efi_guid():
    u = uuid.uuid4()

    b = u.bytes

    data1 = int.from_bytes(b[0:4], byteorder="little")
    data2 = int.from_bytes(b[4:6], byteorder="little")
    data3 = int.from_bytes(b[6:8], byteorder="little")
    data4 = b[8:]

    print("static EFI_GUID SMBIOS_SYS_UUID UNUSED =")
    print("{")
    print(f"    0x{data1:08X},")
    print(f"    0x{data2:04X},")
    print(f"    0x{data3:04X},")
    print("    { " + ", ".join(f"0x{x:02X}" for x in data4) + " }")
    print("};")

if __name__ == "__main__":
    generate_efi_guid()