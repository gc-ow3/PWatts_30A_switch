#!/usr/bin/python3
#
# This is a convenience class supporting GPIO using the FTDI4232 module
#
import os
from time import localtime
from pyftdi import gpio
from pyftdi.gpio import GpioAsyncController
import io
import threading
from inspect import isfunction

class testerGPIO():
	def __init__(self, inputTab:list, outputTab:list):
		self.inputTab = inputTab
		self.outputTab = outputTab
		self.ioFault = False
		self.ioFaultMsg = ""
		self.errorMsg = ""
		self.portMap = {"gpioA": None, "gpioB": None, "gpioC": None, "gpioD": None}
		self.mutex = threading.Lock()
		self.callbackTab = []

		self.pollThread = threading.Thread(target=self._inputPoll, daemon=True)
		self.pollThread.start()

	def _findOutput(self, name:str) -> dict:
		for d in self.outputTab:
			if name == d['name']:
				return (d['port'], d['pin'], d['aState'])
		return None

	def _findInput(self, name:str) -> dict:
		for d in self.inputTab:
			if name == d['name']:
				return (d['port'], d['pin'], d['aState'])
		return None

	def _findCallback(self, name:str) -> int:
		with self.mutex:
			tabLen = len(self.callbackTab)
			for idx in range(0, tabLen):
				if name == self.callbackTab[idx]['name']:
					return idx
		return None

	def _inputPoll(self):
		while not self.ioFault:
			sleep(0.1)
			with self.mutex:
				for d in self.callbackTab:
					inp = self._findInput(d['name'])
					if inp is None:
						continue
					port, pin, aState = inp

					gpio = self.portMap[port]
					if gpio is None:
						continue

					try:
						inp = gpio.read()
					except:
						self.ioFault = True
						self.ioFaultMsg = f"Reading '{port}' failed"
						break

					bitSet = (inp & (1 << pin)) != 0
					curState = d['active']
					newState = curState

					if bitSet:
						if "high" == aState:
							if not curState:
								# Active high input transitions to active state
								newState = True
						else:
							if curState:
								# Active low input transitions to inactive state
								newState = False
					else:
						if "high" == aState:
							if curState:
								# Active high input transitions to inactive state
								newState = False
						else:
							if not curState:
								# Active low input transitions to active state
								newState = True

					# Check for change of active state
					if newState != curState:
						d['active'] = newState
						d['func'](newState, d['data'])

	def ioFaultInfo(self) -> tuple:
		return (self.ioFault, self.ioFaultMsg)

	def setPort(self, name:str, port) -> bool:
		if name not in self.portMap:
			return False
		self.portMap[name] = port
		return True

	def setInputCallback(self, name:str, func, data=None) -> bool:
		if not isfunction(func):
			return False
		ret = self._findInput(name)
		if ret is None:
			return False
		cbDef = {"name":name, "func":func, "data":data, "active":False}

		with self.mutex:
			tabIdx = self._findCallback(name)
			if tabIdx is None:
				self.callbackTab.append(cbDef)
			else:
				self.callbackTab[tabIdx] = cbDef
		return True

	def clearInputCallback(self, name:str) -> bool:
		with self.mutex:
			tabIdx = self._findCallback(name)
			if tabIdx is not None:
				del self.callbackTab[tabIdx]
				return True
		return False

	def resetAllOutputs(self, delay:float=0) -> bool:
		if self.ioFault:
			return False
		for d in self.outputTab:
			if not self.setOutput(d['name'], False):
				return False
		if delay:
			sleep(delay)
		return True

	def setOutput(self, name:str, active:bool, delay:float=0) -> bool:
		if self.ioFault:
			return False
		outp = self._findOutput(name)
		if outp is None:
			return False
		port, pin, aState = outp

		gpio = self.portMap[port]
		if gpio is None:
			return False

		try:
			curData = gpio.read()
		except:
			self.ioFault = True
			self.ioFaultMsg = f"Reading '{port}' failed"
			return False

		pinBit = (1 << pin)
		pinSet = (curData & pinBit) != 0

		newData = curData
		if active:
			# Set output to active state
			if aState == "high" and not pinSet:
				newData |= pinBit
			elif aState == "low" and pinSet:
				newData &= ~pinBit
		else:
			# Set output to inactive state
			if aState == "high" and pinSet:
				newData &= ~pinBit
			elif aState == "low" and not pinSet:
				newData |= pinBit

		if newData != curData:
			# Update the output
			gpio.write(newData)
		if delay:
			sleep(delay)
		return True

	def isActive(self, name:str) -> bool:
		if self.ioFault:
			return None
		inp = self._findInput(name)
		if inp is None:
			return None
		port, pin, aState = inp

		gpio = self.portMap[port]
		if gpio is None:
			return None

		try:
			data = gpio.read()
		except:
			self.ioFault = True
			self.ioFaultMsg = f"Reading '{port}' failed"
			return None

		pinSet = (data & (1 << pin)) != 0
		if pinSet:
			return True if "high" == aState else False
		else:
			return True if "low" == aState else False

	def readPort(self, port:str) -> int:
		if self.ioFault:
			return None
		if port not in self.portMap:
			return None
		gpio = self.portMap[port]
		if gpio is None:
			return None

		try:
			data = gpio.read()
		except:
			self.ioFault = True
			self.ioFaultMsg = f"Reading '{port}' failed"
			return None
		return data

	def writePort(self, port:str, data, delay:float=0) -> bool:
		if self.ioFault:
			return False
		if port not in self.portMap:
			return False
		gpio = self.portMap[port]
		if gpio is None:
			return False

		try:
			gpio.write(data)
		except:
			self.ioFault = True
			self.ioFaultMsg = f"Writing '{port}' failed"
			return False
		if delay:
			sleep(delay)
		return True
