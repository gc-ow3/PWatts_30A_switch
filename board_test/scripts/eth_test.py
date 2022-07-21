#!/usr/bin/python3
import os
import sys
import platform
import socket
import struct
import fcntl
import serial
#from serial import SerialException
import time
#import re

PROMPT = "[cmd]>"
portESP32 = "/dev/ttyUSB1"
if_eth = "eno1"

def openEsp32Port() -> bool:
	global esp32Port

	try:
		esp32Port = serial.Serial(portESP32, 115200, timeout=0.5)
	except:
		print(f"Failed to open serial port {comDevice}")
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
	try:
		while True:
			rd = comPort.read(200)
			if len(rd) == 0:
				break
			sleep(0.1)
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
def sendCommandNoResp(cmd, timeLimit, comPort=None, dbug=False) -> bool:
	resp = sendCommand(cmd, timeLimit, comPort=comPort, dbug=dbug)
	if len(resp) >= 2 and "OK" == resp[-2:]:
		return True
	else:
		if dbug:
			print(resp)
		return False

# Send a command expecting either "PASS" or "FAIL" to be returned
def sendCommandPassFail(cmd, timeLimit, comPort=None, dbug=False) -> bool:
	resp = sendCommand(cmd, timeLimit, comPort=comPort, dbug=dbug)
	if "FAIL" == resp:
		return None
	lines = resp.split("\r\n")
	if lines[-1] == "OK":
		ret = lines[-2].strip()
		return True if "PASS" == ret else False
	else:
		return None

def bdTestReadVersion(comPort=None):
	resp = sendCommand("TST-VER", 2, comPort=comPort, dbug=False)
	if "FAIL" == resp:
		return None
	lines = resp.split("\r\n")
	if lines[-1] == "OK":
		return lines[-2].strip()
	else:
		return None

def cpuReadId(comPort=None):
	resp = sendCommand("CPU-ID", 5, comPort=comPort)
	if "FAIL" == resp:
		return None
	lines = resp.split("\r\n")

	if lines[-1] == "OK":
		return lines[-2].strip()
	else:
		return None

def buttonRead(comPort=None) -> bool:
	resp = sendCommand("BUTTON-READ", 5, comPort=comPort)
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

def atcaSnRead(comPort=None):
	resp = sendCommand("ATCA-SN-READ", 2)
	if "FAIL" == resp:
		return None
	lines = resp.split("\r\n")
	if lines[-1] != "OK":
		return None
	return lines[-2].strip()

def getMacAddress(ifname):
	s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
	info = fcntl.ioctl(s.fileno(), 0x8927, struct.pack('256s', ifname.encode()))
	s.close()
	return info[18:24]

def tstEthernet() -> bool:
	# Because socket doesn't define this
	ETH_P_ALL = 3

	proto = b"\x70\x90"
	myMac = getMacAddress(if_eth)

	# Build the transmit Ethernet packet
	# start with broadcast address
	txData  =  6 * b"\xff"
	# Add our Ethernet MAC address
	txData += myMac
	# Add the magic protocol number 0x7090
	txData += proto
	# Add test data
	txData += 10 * b"This is a test!\n"

	sock = socket.socket(socket.AF_PACKET, socket.SOCK_RAW, socket.htons(ETH_P_ALL))
	sock.bind((if_eth, 0))

	sock.sendall(txData)
	rxData = sock.recv(1514)

	if rxData[0:6] == myMac[0:6] and rxData[12:14] == proto[0:2]:
		print("MAC matches")

	print(f"dst  : {rxData[0]:02x}:{rxData[1]:02x}:{rxData[2]:02x}:{rxData[3]:02x}:{rxData[4]:02x}:{rxData[5]:02x}")
	print(f"src  : {rxData[6]:02x}:{rxData[7]:02x}:{rxData[8]:02x}:{rxData[9]:02x}:{rxData[10]:02x}:{rxData[11]:02x}")
	print(f"proto: {rxData[12]:02x}{rxData[13]:02x}")
	print(f"data : {rxData[14:]}")

	return True

def doTest():
	ret = bdTestReadVersion()
	print(f"Test version: {ret}")

	cpuId = cpuReadId()
	print(f"cpuId: {cpuId}")

	ret = sendCommandPassFail("IOX-TEST", 2)
	print(f"IOX test: {ret}")

	ret = sendCommandPassFail("ATCA-TEST", 2)
	print(f"ATCA test: {ret}")

	ret = sendCommandPassFail("ADC-TEST", 2)
	print(f"ADC test: {ret}")

	ret = sendCommandPassFail("EEPROM-TEST", 2)
	print(f"EERPOM test: {ret}")

	ret = atcaSnRead()
	print(f"ATCA SN: {ret}")

	ret = buttonRead()
	print(f"Button: {ret}")

	for led in ["SYS", "BLE"]:
		for step in ["RED", "GRN", "BLU", "OFF"]:
			cmd = f"LED-{led}-SET {step}"
			sendCommandNoResp(cmd, 2)
			time.sleep(0.25)

	print("Turn on Ethernet")
	ret = sendCommandNoResp("ETH-START", 5)
	tstEthernet()
	print("Turn off Ethernet")
	ret = sendCommandNoResp("ETH-STOP", 5)

	print("Turn on BLE")
	ret = sendCommandNoResp("BLE-ON 0x0AA0", 5)
	time.sleep(10)
	print("Turn off BLE")
	ret = sendCommandNoResp("BLE-OFF", 5)

	return 0

def main():
	if not openEsp32Port():
		return 1
	ret = doTest()
	closeEsp32Port()
	return ret

if __name__ == "__main__":
	ret = main()
	sys.exit(ret)
