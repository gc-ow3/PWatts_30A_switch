import sys
import os
import argparse

def main():
	p = argparse.ArgumentParser()
	p.add_argument("name", type=str, help="Name to assign to the array")
	p.add_argument("input", type=str, help="Path to input file")
	p.add_argument("output", type=str, help="Path to output file")
	args = p.parse_args()

	if not os.path.exists(args.input):
		print(f"File '{args.input}' not found")
		return 1

	arrayName = args.name
	arraySzName = arrayName + "Len"

	with open(args.output, "wt") as outp:
		# First line
		outp.write(f"const unsigned char {arrayName}[] = "  + "{\n")

		with open(args.input, "rb") as inp:
			fileData = inp.read()

		colCt = 0
		byteCt = 0
		for val in fileData:
			if byteCt == 0:
				outp.write("  ")
			elif colCt < 16:
				outp.write(", ")
			else:
				colCt = 0
				outp.write(",\n")
				outp.write("  ")
			outp.write(f"0x{val:02x}")
			byteCt += 1
			colCt += 1
		# close the array
		if colCt < 16:
			outp.write("\n")
		outp.write("};\n")
		# Last line
		outp.write(f"const unsigned int {arraySzName} = sizeof({arrayName});\n")

	print(f"{byteCt} bytes written as array name '{arrayName}'")
	return 0

if __name__ == "__main__":
	ret = main()
	sys.exit(ret)
