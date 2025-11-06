#!/usr/bin/env python3
import sys
import os

def binary_to_header(input_file, output_file, array_name):
    """Convert binary file to C++ header with byte array"""
    try:
        with open(input_file, 'rb') as f:
            data = f.read()

        with open(output_file, 'w') as f:
            f.write(f'#ifndef {array_name.upper()}_H\n')
            f.write(f'#define {array_name.upper()}_H\n\n')
            f.write('#include <cstdint>\n')
            f.write('#include <cstddef>\n\n')
            f.write(f'extern const uint8_t {array_name}[];\n')
            f.write(f'extern const size_t {array_name}_size;\n\n')
            f.write(f'#endif // {array_name.upper()}_H\n')

        # Now create the .cpp file with the actual data
        cpp_file = output_file.replace('.h', '.cpp')
        with open(cpp_file, 'w') as f:
            f.write(f'#include "{os.path.basename(output_file)}"\n\n')
            f.write(f'const uint8_t {array_name}[] = {{\n')

            # Write data in chunks of 16 bytes per line for readability
            for i in range(0, len(data), 16):
                chunk = data[i:i+16]
                hex_values = [f'0x{b:02x}' for b in chunk]
                f.write('    ' + ', '.join(hex_values))
                if i + 16 < len(data):
                    f.write(',\n')
                else:
                    f.write('\n')

            f.write('};\n\n')
            f.write(f'const size_t {array_name}_size = sizeof({array_name});\n')

        print(f"Successfully converted {input_file} to {output_file} and {cpp_file}")
        print(f"Array size: {len(data)} bytes")

    except Exception as e:
        print(f"Error: {e}")
        sys.exit(1)

if __name__ == '__main__':
    if len(sys.argv) != 4:
        print("Usage: python binary_to_header.py <input_file> <output_header> <array_name>")
        sys.exit(1)

    input_file = sys.argv[1]
    output_file = sys.argv[2]
    array_name = sys.argv[3]

    binary_to_header(input_file, output_file, array_name)