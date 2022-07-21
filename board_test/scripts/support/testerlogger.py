#!/usr/bin/python3
import os
from time import localtime

class testerLogger():
	def __init__(self, logItemList, logDir, rootName):
		self.logItemList = logItemList
		self.logDir = logDir
		self.rootName = rootName

		if not os.path.exists(self.logDir):
			os.makedirs(self.logDir)
			os.chmod(logDir, 0o777)

	def reset(self):
		for item in self.logItemList:
			item['value'] = ""

	def setItem(self, name, value) -> bool:
		for item in logItemList:
			if item['name'] == name:
				item['value'] = value
				return True
		print(f"Log item {name} not defined")
		return False

	def getItem(self, name) -> str:
		for item in self.logItemList:
			if item['name'] == name:
				return item['value']
		return None

	def store(self):
		tm = time.localtime()
		dateStamp = f"{tm.tm_year}{tm.tm_mon:02}{tm.tm_mday:02}"

		fPath = os.path.join(self.logDir, f"{self.rootName}_{dateStamp}.csv")

		if not os.path.exists(fPath):
			# Create the log file and build and write the header line to it
			itemCt = 0
			hdr    = ""
			for item in logItemList:
				if itemCt > 0:
					hdr += ","
				hdr += item['name']
				itemCt += 1
			with open(fPath, "w") as fh:
				fh.write(hdr + "\n")
			os.chmod(fPath, 0o777)

		logData = ""
		itemCt = 0
		for item in logItemList:
			if itemCt > 0:
				logData += ","
			if "" == item['value']:
				value = "null"
			else:
				value = item['value']
			logData += value
			itemCt += 1

		# Wrtie the log entry
		with open(fPath, "a") as fh:
			fh.write(logData + '\n')
		return True
