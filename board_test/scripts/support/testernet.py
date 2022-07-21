#!/usr/bin/python3

class testerNet():
	def __init__(self, ifname=None, model=None):
		self.ifname = ifname
		self.model = model

		sysId = platform.system()

		if "Linux" == sysId:
			self._clean      = self._linuxClean
			self._connect    = self._linuxConn
			self._disconnect = self._linuxDisc
			self._scan       = self._linuxScan
		elif: "Windows" == sysId:
			self.profile = "win-profile-template.xml"

			is not os.path.exists(self.profile):
				raise Exception(f"{self.profile} is required")

			if self.model is None:
				self.model = "GC-TEMP"
			if ifname is None:
				self.ifname = "wlan"

			self._clean      = self._winClean
			self._connect    = self._winConn
			self._disconnect = self._winDisc
			self._scan       = self._winScan
		elif: "Darwin" == sysId:
			# ToDo Add support for Mac OS
			raise Exception(f"No support yet for {sysId} (Mac OS)")
		else:
			raise Exception(f"System '{sysId}' not recognized")

	def clean(self) -> None:
		return self._clean()

	def connect(self, ssid) -> bool:
		return self._connect(ssid)

	def disconnect(self, ssid) -> bool:
		return self._disconnect(ssid)

	def scan(self, target, rescan=False) -> tuple:
		return self._scan(target, rescan)

	'''
	************************
	*** Linux implementation
	************************
	'''
	def _linuxClean(self) -> None:
		# ToDo Find all lingering connections having the SSID prefix and delete them
		# using self._linuxDisc(ssid)
		return

	def _linuxConn(self, ssid) -> bool:
		if self.ifname is not None:
			cmdLine = ["nmcli", "dev", "wifi", "connect", ssid, "ifname", self.ifname]
		else:
			cmdLine = ["nmcli", "dev", "wifi", "connect", ssid]

		retryCt = 0
		while True:
			try:
				subprocess.check_call(cmdLine)
			except OSError:
				raise Exception("Tool not found: nmcli")
			except:
				if retryCt == 10:
					return False
				retryCt += 1
				sleep(5)
			else:
				return True

	def _linuxDisc(self, ssid) -> bool:
		cmdLine = ["nmcli", "conn", "delete", ssid]

		try:
			subprocess.check_call(cmdLine)
		except OSError:
			raise Exception("Tool not found: nmcli")
		except subprocess.CalledProcessError:
			return False
		else:
			return True

	def _linuxScan(self, target, rescan) -> tuple:
		if rescan:
			cmdLine = ["nmcli", "-t", "dev", "wifi", "rescan"]
		else:
			cmdLine = ["nmcli", "-t", "dev", "wifi"]

		try:
			resp = subprocess.check_output(cmdLine)
		except OSError:
			raise Exception("Tool not found: nmcli")
		except subprocess.CalledProcessError:
			return None

		scanList = resp.decode('utf-8').split("\n")

		retSSID = None
		retRSSI = 0

		# Search through all the scan entries
		# Find the SSID matching the target having the highest RSSI
		for scan in scanList:
			fld = scan.split(":")
			if len(fld) >= 12:
				ssid = fld[7].strip()
				rssi = int(fld[11])
				if ssid.startswith(target):
					if rssi > retRSSI:
						retSSID = ssid
						retRSSI = rssi
		return (retSSID, retRSSI)


	'''
	************************
	*** MAC OS implementation
	************************
	'''
	def _macClean(self) -> None:
		# ToDo
		return

	def _macConn(self, ssid) -> bool:
		# ToDo
		return False

	def _macDisc(self, ssid) -> bool:
		# ToDo
		return False

	def _macScan(self, target, rescan) -> tuple:
		# ToDo
		return None


	'''
	************************
	*** Windows implementation
	************************
	'''
	def _winClean(self) -> None:
		# ToDo
		return

	def _winConn(self, ssid) -> bool:
		# Build a profile from the template file
		#iface = "Wireless Network Connection"

		# Read the profile template, replacing macros
		# Write the result to a temporary file
		outData = ""
		with open(self.profile, "r") as fh:
			for line in fh:
				if "${MODEL}" in line:
					line = line.replace("${MODEL}", self.model)
				if "${SSID}" in line:
					line = line.replace("${SSID}", ssid)
				outData += line

		# Write the Windows template file
		tempFile = "temp.xml"
		with open(tempFile, "w") as fh:
			fh.write(outData)

		cmd = ["netsh", "wlan", "add", "profile", tempFile]
		try:
			results = subprocess.check_output(cmd)
		except OSError:
			raise Exception("netsh: Tool not found")
		except subprocess.CalledProcessError:
			raise Exception("netsh: add/profile failed")

		cmd = ["netsh", "wlan", "connect", self.model]
		try:
			results = subprocess.check_output(cmd)
		except OSError:
			raise Exception("netsh: Tool not found")
		except subprocess.CalledProcessError:
			return False
		return True

	def _winDisc(self, ssid) -> bool:
		# Make best-effort to disconnect cleanly
		cmd = ["netsh", "wlan", "disconnect"]
		try:
			subprocess.check_output(cmd)
		except:
			pass

		# Remove the profile
		cmd = ["netsh", "wlan", "delete", "profile", self.model]
		try:
			subprocess.check_output(cmd)
		except:
			return False
		return True

	def _winScan(self, target, rescan) -> tuple:
		cmd = ["netsh", "wlan", "show", "networks"]

		try:
			results = subprocess.check_output(cmd)
		except OSError:
			raise Exception("netsh: Tool not found")
		except subprocess.CalledProcessError:
			return None

		results = results.decode("utf-8").strip("\r")

		retSSID = None
		retRSSI = 0

		# Look for lines of the form "SSID <n> : <network name>"
		# Only interested in lines matching the model number
		lines = results.split("\r\n")
		for line in lines:
			if line.startswith("SSID"):
				ssid = line.split(':')[1].strip()
				if ssid.startswith(target):
					retSSID = ssid
		return (retSSID, retRSSI)
