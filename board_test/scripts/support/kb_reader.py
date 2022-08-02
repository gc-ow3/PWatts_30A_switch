#!/usr/bin/python3
import os
import threading
import queue

class kbReader():
	def __init__(self):
		self.enabled = False
		self.inpQ = queue.Queue()
		self.thread = threading.Thread(target=self.__kbReader, daemon=True)
		#self.thread.start()

	def start(self):
		self.thread.start()

	def enable(self):
		if self.enabled:
			return
		# Read and discard any residual data
		while self.inpQ.qsize() > 0:
			self.inpQ.get()
		# Enable reading data into the queue
		self.enabled = True

	def disable(self):
		if not self.enabled:
			return
		self.enabled = False
		while self.inpQ.qsize() > 0:
			self.inpQ.get()

	def isEnabled(self):
		return self.enabled

	def __kbReader(self):
		while True:
			try:
				# The input function blocks until there is a line entered
				inp = input()
				# Discard data unless actively reading
				if self.enabled:
					self.inpQ.put(inp)
			except:
				pass

	def get(self) -> str:
		if self.inpQ.qsize() > 0:
			return self.inpQ.get()
		else:
			return ""
