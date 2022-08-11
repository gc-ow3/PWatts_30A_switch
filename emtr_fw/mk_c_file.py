import sys
import os
import argparse

def main():
	p = argparse.ArgumentParser()
	#p.add_argument("-h", "--header", type=str, default=None, help="Optional header line, will be prefixed with '//'")
	#p.add_argument("-n", "--name", type=str, default=None, help="Optional name of the array. Default will be derived from file name")
	p.add_argument("input", type=str, help="Path to input file")
	p.add_argument("output", type=str, help="Path to output file")
	args = p.parse_args()

	if not os.path.exists(args.input):
		print(f"File '{args.input}' not found")
		return 1

	arrayName = "emtrFwBin"
	arraySzName = "emtrFwBinLen"

	with open(args.output, "wt") as outp:
		# First line
		outp.write(f"const unsigned char {arrayName}[] = "  + "{\n")

		colCt = 0
		byteCt = 0
		with open(args.input, "rb") as inp:
			for b in inp:
				if colCt == 0:
					outp.write("  ")
				if byteCt > 0:
					outp.write(',')
					if colCt == 15:
						outp.write('\n')
				if colCt < 15:
					outp.write(' ')
				outp.write(f"0x{b:02x}")
				byteCt += 1
				colCt += 1
				if colCt == 16:
					print("\n")
					colCt = 0
			# close the array
			if colCt != 0:
				outp.write("\n")
			outp.write("};\n")
		# Last line
		outp.write(f"const unsigned int {arraySzName} = sizeof({arrayName});\n")


	return 1

if __name__ == "__main__":
	ret = main()
	sys.exit(ret)
