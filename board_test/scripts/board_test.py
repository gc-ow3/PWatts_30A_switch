#!/usr/bin/python3
import os
import sys
import platform
import socket
import serial
import platform
#from serial import SerialException
import time
from time import sleep
import json
import configparser
import argparse
import signal
import struct
import asyncio
import ctypes
from bleak import BleakScanner
from packaging import version

from support.kb_reader import kbReader

sysName = platform.system()

if "Linux" == sysName:
	print("Set up for Linux")
	import fcntl
	isAdmin = (os.getuid() == 0)
elif "Windows" == sysName:
	print("Set up for Windows")
	from getmac import get_mac_address as gma
	from scapy.all import *
	isAdmin = (ctypes.windll.shell32.IsUserAnAdmin() != 0)
else:
	print(f'System "{sysName}" is not supported')
	sys.exit(1)

PROMPT = "[cmd]>"

def loadConfig() -> bool:
	global testConfig

	# Read the configuration file
	try:
		testConfig = configparser.ConfigParser()
		testConfig.read("./scripts/config.txt")
	except configparser.ParsingError as e:
		print(e)
		return False
	except:
		print("Other error")
		return False

	if "Linux" == sysName:
		comm = testConfig['COMM_LINUX']
	elif "Windows" == sysName:
		comm = testConfig['COMM_WIN']
	else:
		print(f'System "{sysName}" is not supported')
		return False

	global portESP32
	portESP32 = comm['esp32_port']

	global portSerial1
	global baudSerial1
	portSerial1 = comm['serial1_port']
	baudSerial1 = comm['serial1_baud']

	global portSerial2
	global baudSerial2
	portSerial2 = comm['serial2_port']
	baudSerial1 = comm['serial2_baud']

	global iface_eth
	iface_eth = comm['if_eth']

	global bleSvcId
	bleSvcId = testConfig['BLE']['svc_id']

	return True

def openEsp32Port() -> bool:
	global esp32Port

	try:
		esp32Port = serial.Serial(portESP32, 115200, timeout=0.5)
	except:
		print(f'Failed to open serial port "{portESP32}"')
		esp32Port = None
		return False
	return True

def closeEsp32Port():
	global esp32Port
	if esp32Port is not None:
		esp32Port.close()
		esp32Port = None

def esp32ResetHold(active:bool):
	esp32Port.dtr = active

def esp32Reset(delay=5):
	esp32Port.dtr = True
	sleep(0.1)
	esp32Port.dtr = False
	sleep(delay)

def sendCommand(cmd, timeLimit, comPort=None, dbug=False):
	# Default to ESP32 port if not overridden
	if comPort is None:
		comPort = esp32Port

	# Flush any residual noise
	if dbug:
		print("Flush input")
	try:
		while True:
			rd = comPort.read()
			if len(rd) == 0:
				break
	except:
		pass

	if dbug:
		print(cmd)
	comPort.write(cmd.encode("utf-8") + b"\r")

	endTime = time.time() + timeLimit
	result  = ""
	while True:
		if time.time() > endTime:
			print(f"Timed out waiting for response to {cmd}")
			return "FAIL"
		rd = comPort.read()
		if len(rd) > 0:
			rdStr = rd.decode("utf-8")
			if dbug:
				print(rdStr, end="")
			result += rdStr
			if PROMPT in result:
				break
		else:
			time.sleep(0.2)

	#print(result)

	if result.endswith(PROMPT):
		result = result[:-len(PROMPT)]
	elif result.startswith(PROMPT + cmd + "\r\n"):
		result = result[len(PROMPT) + len(cmd) + 2:]
	elif result.endswith(PROMPT + cmd):
		result = result[:-(len(PROMPT) + len(cmd) + 2)]

	# Strip trailing white space
	result = result.rstrip()

	#print(f"res after parse\n{result}")

	if len(result) == 0:
		print(f"No response returned for {cmd}")
		result = "FAIL"

	return result

# Send a command not expecting to read back data
def sendCommandNoResp(cmd, timeLimit, dbug=False) -> bool:
	resp = sendCommand(cmd, timeLimit, dbug=dbug)
	if len(resp) >= 2 and "OK" == resp[-2:]:
		return True
	else:
		if dbug:
			print(resp)
		return False

# Send a command expecting either "PASS" or "FAIL" to be returned
def sendCommandPassFail(cmd, timeLimit) -> bool:
	resp = sendCommand(cmd, timeLimit)
	if "FAIL" == resp:
		return None
	lines = resp.split("\r\n")
	if lines[-1] == "OK":
		ret = lines[-2].strip()
		return True if "PASS" == ret else False
	else:
		return None

def bdTestReadVersion():
	resp = sendCommand("TST-VER", 2)
	if "FAIL" == resp:
		return None
	lines = resp.split("\r\n")
	if lines[-1] == "OK":
		return lines[-2].strip()
	else:
		return None

def cpuReadId():
	resp = sendCommand("CPU-ID", 5)
	if "FAIL" == resp:
		return None
	lines = resp.split("\r\n")

	if lines[-1] == "OK":
		return lines[-2].strip()
	else:
		return None

def emtrReadVersion(comPort=None, dbug=False):
	resp = sendCommand("EMTR-READ-VER", 2, comPort=comPort, dbug=dbug)
	#print(f"READ-EMTR-VER response: {resp}")
	if "FAIL" == resp:
		return None
	lines = resp.split("\r\n")
	#print(lines)
	if lines[-1] != "OK":
		#print(resp)
		return None
	vers = lines[1].split(",")
	if len(vers) != 2:
		return None
	return {"blVer" : vers[0][3:].strip(), "fwVer" : vers[1][3:].strip()}

def emtrReadEnergy(comPort=None):
	resp = sendCommand("EMTR-READ-ENERGY", 2, comPort=comPort)
	if "FAIL" == resp:
		return None

	ln = resp.split("\r\n")
	if 3 == len(ln) and "OK" == ln[2].strip():
		eData = ln[1].strip().split(",")
		# 0 volts
		# 1 amps
		# 2 watts
		# 3 power factor 
		# 4 seconds powered
		# 5 seconds triacs on
		return {"volts" : float(eData[0]), "amps" : float(eData[1])}

	return None


def emtrSetGain(uGain, iGain) -> bool:
	if not sendCommandNoResp(f"EMTR-SET-U-GAIN {uGain}", 2):
		return False
	if not sendCommandNoResp(f"EMTR-SET-I-GAIN {iGain}", 2):
		return False
	return True

def emtrSaveCalibration():
	return sendCommandNoResp("EMTR-SAVE-CAL", 5):

def emtrReadCalibration(comPort=None):
	resp = sendCommand("EMTR-READ-CAL", 2, comPort=comPort)
	if "FAIL" == resp:
		return None

	ln = resp.split("\r\n")
	if 3 == len(ln) and "OK" == ln[2].strip():
		cal = ln[1].strip().split(",")
		# 0 u_gain
		# 1 i_gain
		# 2 hcci
		return {
			"u_gain" : round(float(cal[0]), 4),
			"i_gain" : round(float(cal[1]), 4),
			"hcci"   : int(cal[2]),
			}

	return None

def rdInput(tDesc):
	cmd = f"INPUT-READ {tDesc['name']}"
	resp = sendCommand(cmd, 2)
	if "FAIL" == resp:
		return None
	lines = resp.split("\r\n")

	if lines[-1] != "OK":
		return None

	try:
		payload = json.loads(lines[-2].strip())
	except:
		return None

	return payload['active']

''' Input test descriptors '''
inpTestDesc = [
	{"title": "Button 1", "name": "BTN1"},
	{"title": "Button 2", "name": "BTN2"},
	{"title": "Switch 1", "name": "SW1"},
]

def inpTest(tDesc):
	print(f"Test {tDesc['name']}")

	# Initial read should be inactive
	rd = readInput(param)
	if rd is None:
		return False
	elif rd:
		print("  Input is active when it should be not active")
		return False

	print("  >>>>> Activate the input, or enter Q to skip <<<<<")
	kb.enable(True)
	ret = False
	while True:
		if kb.get() in ["Q", "q"]:
			break
		x = rdInp()
		if x is None:
			break
		elif x:
			ret = True
			break
		else:
			time.sleep(0.25)
	kb.enable(False)
	return ret


def doTest(kb, arg):
	ret = cpuReadId()
	print(f"cpuId: {ret}")
	if ret is None:
		return 1

	print("Input tests")
	for t in inpTestDesc:
		if not inpTest(t):
			return 1

	print("LED test")
	for led in ["SYS", "BLE"]:
		for step in ["RED", "GRN", "BLU", "OFF"]:
			cmd = f"LED-{led}-SET {step}"
			ret = sendCommandNoResp(cmd, 2)
			if ret is None or not ret:
				return 1
			time.sleep(0.25)

	print("Test done")
	return 0

def tstBoard(kb, arg) -> int:
	ret = doTest(arg)

	print("")
	if 0 == ret:
		print(">>>> PASS <<<<<")
	else:
		print(">>>>> FAIL <<<<<")
	print("")

	return ret

async def bleScan(target):
	devList = await BleakScanner.discover()
	for d in devList:
		if d.name == target:
			return (d.name, d.address, d.rssi)
	return None

def tstBle(arg) -> bool:
	print("Turn on BLE")
	ret = sendCommandNoResp(f"BLE-ON 0x{bleSvcId}", 5)
	if ret is None or not ret:
		print("  Failed")
		return False

	target = f"PWATTS-{bleSvcId}"
	print(f"Listen for {target}")
	match = asyncio.run(bleScan(target))

	time.sleep(10)
	print("Turn off BLE")
	ret = sendCommandNoResp("BLE-OFF", 5)
	if ret is None or not ret:
		print("  Failed")
		return False

	if match is not None:
		print(f"'{target}' RSSI = {match[2]}")
		return True
	else:
		print(f"BLE name '{target}' not found")
		return False

userHalt = False
def sigHandler(sigNum, frame):
	global userHalt
	if not userHalt:
		userHalt = True
		print(f"Signal {sigNum} caught - Halting")
	sys.exit(0)
 
def main():
	parser = argparse.ArgumentParser()
	subParse = parser.add_subparsers(dest="mode", choices=["board", "ble"], help="Test mode")

	brdTest = subParse.add_parser("board", help="Test all board functions")

	bleTest = subParse.add_parser("ble", help="Test BLE")

	arg = parser.parse_args()

	if not loadConfig():
		return 1

	signal.signal(signal.SIGINT, sigHandler)

	if not openEsp32Port():
		return 1

	ret = bdTestReadVersion()
	print(f"Test firmware version: {ret}")
	if ret is None:
		return 1

	minVersion = "0.1.0"
	if version.parse(ret) < version.parse(minVersion):
		print(f"Board test firmware version {minVersion} or higher is required")
		return 1

	kb = kbReader()
	kb.start()

	if arg.mode == "board":
		ret = tstBoard(kb, arg)
	elif arg.mode == "ble":
		ret = tstBle(arg)
	else:
		print(f"Test mode '{arg.mode}' not implemented")
		ret = 1

	closeEsp32Port()
	return ret

if __name__ == "__main__":
	ret = main()
	sys.exit(ret)
