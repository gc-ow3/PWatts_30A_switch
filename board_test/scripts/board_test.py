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

def buttonRead() -> bool:
	resp = sendCommand("BUTTON-READ", 5)
	if "FAIL" == resp:
		return None
	lines = resp.split("\r\n")

	if lines[-1] != "OK":
		return None
	if lines[-2].startswith("BTN:0"):
		return False
	elif lines[-2].startswith("BTN:1"):
		return True
	else:
		return None

def atcaSnRead():
	resp = sendCommand("ATCA-SN-READ", 2)
	if "FAIL" == resp:
		return None
	lines = resp.split("\r\n")
	if lines[-1] != "OK":
		return None
	return lines[-2].strip()

def eepromInfoRead():
	resp = sendCommand("EEPROM-INFO", 2)
	if "FAIL" == resp:
		return None
	lines = resp.split("\r\n")
	if lines[-1] != "OK":
		return None
	return lines[-2].strip()

def eepromInfoSet(serNum):
	sn = f"{int(serNum):06}"
	tm = time.localtime()
	tStamp = f"{tm.tm_year}-{tm.tm_mon:02}-{tm.tm_mday:02}"

	tries = 0
	while not sendCommandNoResp(f"EEPROM-SET KA1-{sn} {tStamp}", 2):
		if tries == 4:
			print("Failed to set EEPROM info")
			return False
		tries += 1
		time.sleep(1)
	return True

def getMacAddress(ifname):
	if "Linux" == sysName:
		s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
		info = fcntl.ioctl(s.fileno(), 0x8927, struct.pack('256s', ifname.encode()))
		s.close()
		return info[18:24]
	elif "Windows" == sysName:
		return bytes.fromhex(gma().replace(':', ''))
	else:
		return None

ethProto = b"\x70\x90"

def buildEthTestPacket(testData):
	myMac = getMacAddress(iface_eth)

	# Build the transmit Ethernet packet
	# start with broadcast address
	txData  =  6 * b"\xff"
	# Add our Ethernet MAC address
	txData += myMac
	# Add the magic protocol number 0x7090
	txData += ethProto
	# Add test data
	txData += testData
	return txData

def tstEthernetLinux(myMac, testData) -> bool:
	# Because socket doesn't define this
	ETH_P_ALL = 3

	sock = socket.socket(socket.AF_PACKET, socket.SOCK_RAW, socket.htons(ETH_P_ALL))
	sock.bind((iface_eth, 0))

	txData = buildEthTestPacket(testData)
	sock.sendall(txData)
	rxData = sock.recv(1514)

	if rxData[0:6] == myMac[0:6] and rxData[12:14] == ethProto[0:2] and rxData[14:] == testData:
		ret = True
		print("  Test passed")
	else:
		ret = False
		print("  Test failed")
		print(f"    dst  : {rxData[0]:02x}:{rxData[1]:02x}:{rxData[2]:02x}:{rxData[3]:02x}:{rxData[4]:02x}:{rxData[5]:02x}")
		print(f"    src  : {rxData[6]:02x}:{rxData[7]:02x}:{rxData[8]:02x}:{rxData[9]:02x}:{rxData[10]:02x}:{rxData[11]:02x}")
		print(f"    proto: {rxData[12]:02x}{rxData[13]:02x}")
		print(f"    data : {rxData[14:]}")

	return ret


def tstEthernetWin(myMac, testData) -> bool:
	# Using the scapy library
	ifaceEth = "Ethernet 2"
	#ifaceEth = IFACES.dev_from_index(12)
	print(f"ifaceEth {ifaceEth}")
	sock = conf.L2socket(iface=ifaceEth)

	txData = buildEthTestPacket(testData)
	sock.send(txData)

	rxPkt = sock.recv(1514)

	if rxPkt.dst == myMac[0:6] and rxPkt.type == ethProto[0:2] and bytes(rxPkt.payload) == testData:
		ret = True
		print("  Test passed")
	else:
		ret = False
		print("  Test failed")
		print(f"    dst  : {rxPkt.dst}")
		print(f"    src  : {rxPkt.src}")
		print(f"    proto: {rxPkt.type:04X}")
		print(f"    data : {bytes(rxPkt.payload)}")

	return ret

def tstEthernet() -> bool:
	if sysName == "Linux":
		tstFunc = tstEthernetLinux
	elif sysName == "Windows":
		tstFunc = tstEthernetWin
	else:
		print(f"Ethernet test not implemented for {sysName}")
		return False

	myMac = getMacAddress(iface_eth)
	if myMac is None:
		return False

	print("Turn on Ethernet")
	ret = sendCommandNoResp("ETH-START", 5)
	if ret is None or not ret:
		return False

	testData = 10 * b"This is a test!\n"

	passed = tstFunc(myMac, testData)

	print("Turn off Ethernet")
	ret = sendCommandNoResp("ETH-STOP", 5)
	if ret is None or not ret:
		return False

	return passed

async def bleScan(target):
	devList = await BleakScanner.discover()
	for d in devList:
		if d.name == target:
			return (d.name, d.address, d.rssi)
	return None

def tstBle() -> bool:
	print("Turn on BLE")
	ret = sendCommandNoResp(f"BLE-ON 0x{bleSvcId}", 5)
	if ret is None or not ret:
		print("  Failed")
		return False

	target = f"AQUARIAN-{bleSvcId}"
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

def doTest(kb, arg):
	ret = cpuReadId()
	print(f"cpuId: {ret}")
	if ret is None:
		return 1

	ret = sendCommandPassFail("EEPROM-TEST", 2)
	print(f"EERPOM test: {ret}")
	if ret is None or not ret:
		return 1

	if not eepromInfoSet(arg.sernum):
		return 1

	ret = eepromInfoRead()
	print(f"INFO: {ret}")
	if ret is None:
		return 1

	ret = sendCommandPassFail("IOX-TEST", 2)
	print(f"IOX test: {ret}")
	if ret is None or not ret:
		return 1

	ret = sendCommandPassFail("ATCA-TEST", 2)
	print(f"ATCA test: {ret}")
	if ret is None or not ret:
		return 1

	ret = atcaSnRead()
	print(f"ATCA SN: {ret}")
	if ret is None:
		return 1

	ret = sendCommandPassFail("ADC-TEST", 2)
	print(f"ADC test: {ret}")
	if ret is None or not ret:
		return 1

	print("Button test")
	ret = buttonRead()
	if ret is None:
		print("Failed to read button")
		return 1
	elif ret:
		print("  Button is stuck")
		return 1

	print("  >>>>> Press the button, or enter Q to skip <<<<<")
	kb.enable(True)
	ret = False
	while not ret:
		if "Q" in kb.get():
			return 1
		x = buttonRead()
		if x is None:
			return 1
		elif x:
			ret = True
		else:
			time.sleep(0.25)
	kb.enable(False)

	print("LED test")
	for led in ["SYS", "BLE"]:
		for step in ["RED", "GRN", "BLU", "OFF"]:
			cmd = f"LED-{led}-SET {step}"
			ret = sendCommandNoResp(cmd, 2)
			if ret is None or not ret:
				return 1
			time.sleep(0.25)

	tstEthernet()

	print("Test done")
	return 0

userHalt = False
def sigHandler(sigNum, frame):
	global userHalt
	if not userHalt:
		userHalt = True
		print(f"Signal {sigNum} caught - Halting")
	sys.exit(0)
 
def handleBoard(kb, arg) -> int:
	ret = doTest(kb, arg)

	print("")
	if 0 == ret:
		print(">>>> PASS <<<<<")
	else:
		print(">>>>> FAIL <<<<<")
	print("")

	return ret

def handleADC(arg) -> int:
	lineCt = 0
	cmd = "ADC-READ-ALL"
	while not userHalt:
		ret = sendCommand(cmd, 2)
		if ret is None or ret == "FAIL":
			print("Command execution failed")
		else:
			lines = ret.split("\r\n")
			if lines[-1] == "OK":
				if lineCt % 10 == 0:
					print("")
					print("  PRESS1      PRESS2      PRESS3      PRESS4       TEMP        COND")
					print(" RAW   CAL   RAW   CAL   RAW   CAL   RAW   CAL   RAW   CAL   RAW   CAL  ")
					print("----  ----  ----  ----  ----  ----  ----  ----  ----  ----  ----  ----")
				x = json.loads(lines[-2])
				# As of board test firware 1.20.0 ADC-READ-ALL returns JSON in this form:
				# [[ch0_ADC_Raw, ch0_mvRaw, ch0_ADC_Cal, ch0_mvCal], [ch1_XXX, ...], ...]
				for i in range(6):
					print(f"{x[i][0]:4}  {x[i][2]:4}  ", end="")
				print("")
				lineCt += 1
			else:
				print(f"{cmd} failed")
			time.sleep(2)
	return 0

def handleDigi(arg) -> int:
	lineCt = 0
	cmd = "DIGI-READ-ALL"
	while not userHalt:
		ret = sendCommand(cmd, 2)
		if ret is None or ret == "FAIL":
			print("Command execution failed")
		else:
			lines = ret.split("\r\n")
			if lines[-1] == "OK":
				if lineCt % 10 == 0:
					print("")
					print("   FLOW1     FLOW2     FLOW3")
					print("--------  --------  --------")
				x = json.loads(lines[-2])
				print(f"{x[0]:8}  {x[1]:8}  {x[2]:8}")
				lineCt += 1
			else:
				print(f"{cmd} failed")
			time.sleep(2)
	return 0

def serialTest(portName, baud) -> bool:
	sendData = "This is the serial port test\n"

	try:
		 port = serial.Serial(portName, baud, timeout=0.5)
	except:
		print(f"Failed to open serial port '{portName}'")
		return False

	port.write(sendData.encode('UTF-8'))
	sleep(0.1)
	rxData = port.readline().decode('UTF-8')

	port.close()

	if len(rxData) == 0:
		print(f"Serial test on {portName} received no data")
		return False
	if rxData == sendData:
		return True
	else:
		print(f"Serial test on {portName} received wrong data: {rxData}")
		return False

def tstSerial1(arg) -> bool:
	'''Serial 1 loopback test'''
	if not sendCommandNoResp(f"SER-TEST --chan 1 --baud {arg.baud} --mode loop", 2):
		return False
	ret = serialTest(portSerial1, arg.baud)
	sendCommandNoResp(f"SER-TEST --chan 1 --mode off", 2)
	return ret

def tstSerial2(arg) -> bool:
	'''Serial 2 loopback test'''
	if not sendCommandNoResp(f"SER-TEST --chan 2 --baud {arg.baud} --mode loop", 2):
		return False
	ret = serialTest(portSerial2, arg.baud)
	sendCommandNoResp(f"SER-TEST --chan 2 --mode off", 2)
	return ret

def spySerial(arg, kb) -> bool:
	sendCommandNoResp(f"SER-TEST --chan {arg.port} --baud {arg.baud} --mode spy", 2)
	lineCt = 0
	print("Enter 'Q' to quit")
	kb.enable(True)
	while kb.get() not in ['Q', 'q']:
		sleep(0.1)
		while True:
			rxData = esp32Port.read().decode('UTF-8')
			if len(rxData) > 0:
				print(rxData, end="")
				if '\n' in rxData:
					lineCt += 1
					if lineCt % 20 == 0:
						print("Enter 'Q' to quit")
			else:
				break
	kb.enable(False)
	sendCommandNoResp(f"SER-TEST --chan {arg.port} --mode off", 2)
	return True

def main():
	parser = argparse.ArgumentParser()
	subParse = parser.add_subparsers(dest="mode", help="Test mode")

	brdTest = subParse.add_parser("board", help="Test all board functions")
	brdTest.add_argument(
		"sernum",
		help="Board serial number - will be written to EEPROM"
	)

	bleTest = subParse.add_parser("ble", help="Test BLE")

	bleTest = subParse.add_parser("eth", help="Test Ethernet")

	adcTest = subParse.add_parser("adc", help="Poll and report ADC readings")

	digiTest = subParse.add_parser("digi", help="Poll and report digital readings")

	serLoopTest = subParse.add_parser("ser_loop", help="Serial loopback test")
	serLoopTest.add_argument("--baud", type=int, default=9600, help="Baud rate (default 9600)")
	serLoopTest.add_argument("port", type=int, choices=[1,2], help="Port number: 1 or 2")

	serSpyTest = subParse.add_parser("ser_spy", help="Serial spy")
	serSpyTest.add_argument("--baud", type=int, default=9600, help="Baud rate (default 9600)")
	serSpyTest.add_argument("port", type=int, choices=[1,2], help="Port number: 1 or 2")

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

	minVersion = "1.20.0"
	if version.parse(ret) < version.parse(minVersion):
		print(f"Board test firmware version {minVersion} or higher is required")
		return 1

	'''
	needAdmin = False

	# ToDo Identify any test modes requiring admin privileges

	if needAdmin and not isAdmin:
		print("Must run as administrator")
		return 1
	'''

	kb = kbReader()
	kb.start()

	if arg.mode == "board":
		ret = handleBoard(kb, arg)
	elif arg.mode == "adc":
		ret = handleADC(arg)
	elif arg.mode == "digi":
		ret = handleDigi(arg)
	elif arg.mode == "ble":
		ret = tstBle()
	elif arg.mode == "eth":
		ret = tstEthernet()
	elif arg.mode == "ser_loop":
		print(f"UART{arg.port} Loopback Test")
		if 1 == arg.port:
			if tstSerial1(arg):
				print("  PASS")
				ret = 0
			else:
				print("  FAIL")
				ret = 1
		elif 2 == arg.port:
			if tstSerial2(arg):
				print("  PASS")
				ret = 0
			else:
				print("  FAIL")
				ret = 1
	elif arg.mode == "ser_spy":
		print(f"UART{arg.port} Spy Test")
		if spySerial(arg, kb):
			ret = 0
		else:
			ret = 1
	else:
		print(f"Test mode '{arg.mode}' not implemented")
		ret = 1

	closeEsp32Port()
	return ret

if __name__ == "__main__":
	ret = main()
	sys.exit(ret)
