#!/bin/python

import os
import sys
import time
import datetime
from optparse import OptionParser


class Command(object):
	""" Run a command and capture its output string, error string and exit status"""
	
	def __init__(self, command):
		self.command = command
	
	def run(self, shell=True):
		import subprocess as sp
		process = sp.Popen(self.command, shell = shell, stdout = sp.PIPE, \
				   stderr = sp.PIPE)
		self.pid = process.pid
		self.output, self.error = process.communicate()
		self.failed = process.returncode
		return self
		
	@property
	def returncode(self):
		return self.failed
		
def powerOffVM(vmIdStr):	
	powerOffCmdStr = "vim-cmd vmsvc/power.off " + vmIdStr
	res = Command(powerOffCmdStr).run()
	if res.failed:
		print "Error: Unable to power off VM: ", res.error
		sys.exit(1)
	
def unregisterVM(vmIdStr):	
	unregisterCmdStr = "vim-cmd vmsvc/unregister " + vmIdStr
	res = Command(unregisterCmdStr).run()
	if res.failed:
		print "Error: Unable to unregister the VM: ", res.error
		sys.exit(1)
		
class powerCycleFailure(Exception):
	def __init__(self, vmId):
		self.vmId = vmId
	def __str__(self):
		return repr(self.vmId)
	
		
def createVMXFileAndRegister(vmxFile, memSizeMB, cpuCount):
	vmxGenCmdStr =  "sed -i  \"s/^memsize = \"[0-9]*\".*$/memsize = \\\""
	vmxGenCmdStr += str(memSizeMB)
	vmxGenCmdStr += "\\\"/g\" " + vmxFile
	print vmxGenCmdStr

	res = Command(vmxGenCmdStr).run()
	if res.failed:
		print "Error: ", res.error, "; Unable to change memory size"
		sys.exit(1)

	vmxGenCmdStr =  "sed -i  \"s/^sched.cpu.affinity = \"[0-9]*\".*$/memsize = \\\""
	vmxGenCmdStr += str(memSizeMB)
	vmxGenCmdStr += "\\\"/g\" " + vmxFile
	print vmxGenCmdStr

	res = Command(vmxGenCmdStr).run()
	if res.failed:
		print "Error: ", res.error, "; Unable to change memory size"
		sys.exit(1)
		
	registerVMCmdStr = "vim-cmd solo/register /vmfs/volumes/solidDB-VM/WIN2/WIN2/WIN2.vmx"
	res = Command(registerVMCmdStr).run()
	if res.failed:
		print "Error: ", res.error, "; Unable to register the VM"
		sys.exit(1)
	
	vmIdStr = res.output
	return vmIdStr
	
def runVMPowerCycle(vmIdStr):

	print "VM Id is ", vmIdStr

	powerOnCmdStr = "vim-cmd vmsvc/power.on " + vmIdStr
	res = Command(powerOnCmdStr).run()
	if res.failed:
		print "Error: ", res.error, "; Unable to power on the VM"
		raise powerCycleFailure(vmIdStr)

	logStartTime = ""
	powerOffTime = ""
	powerOnTime = ""
	guestOSOnTime = ""
	swapStartTime = ""
	swapEndTime = ""
	spinTime =  0
	while True:
		time.sleep(1)
		res = Command("grep \"VMX has left the building\" vmware.log").run()
		if res.output:
			powerOffTime = res.output.split('|')[0]
			break
		else:
			spinTime += 1
		if (spinTime > 3000):
			print "Error: timed out"
			raise powerCycleFailure(vmIdStr)

	res = Command("grep \"vm.powerOnTimeStamp\" vmware.log").run()
	if res.failed:
		print "Error: unable to query vmware.log file"
		raise powerCycleFailure(vmIdStr)
	powerOnTime = res.output.split('|')[0]
	
	res = Command("grep \"Log for VMware ESX\" vmware.log").run()
	if res.failed:
		print "Error: unable to query vmware.log file"
		raise powerCycleFailure(vmIdStr)
	logStartTime = res.output.split('|')[0]
	
	res = Command("grep \"Guest OS Booted\" vmware.log").run()
	if res.failed:
		print "Error: unable to query vmware.log file"
		raise powerCycleFailure(vmIdStr)
	guestOSOnTime = res.output.split('|')[0]
	
	res = Command("grep \"generating normal swap file\" vmware.log").run()
	if res.failed:
		print "Error: unable to query vmware.log file"
		raise powerCycleFailure(vmIdStr)
	swapStartTime = res.output.split('|')[0]

	res = Command("grep \"Monitor64_PowerOn\" vmware.log").run()
	if res.failed:
		print "Error: unable to query vmware.log file"
		raise powerCycleFailure(vmIdStr)
	swapEndTime = res.output.split('|')[0]
	return {'swapStart' : swapStartTime, 'swapEnd': swapEndTime, \
		'powerOn' : powerOnTime, 'powerOff' : powerOffTime, \
		'logStart' : logStartTime , 'guestOSOn' : guestOSOnTime}

MaxMemorySize = 4080000
MaxCPUs = 64
baseMemSize = 1024
memSteps = 0
numVCPUs = 4
cpuSteps = 0
vmxFileName = ""


parser = OptionParser()
parser.add_option("-t", "--numThreads", dest="numThreads", type='int',
			help="Number of threads")
parser.add_option("-i", "--numIters", dest="numIters", type='int',
			help="Number of iterations")
parser.add_option("-c", "--collSteps", dest="collisionSteps", type='int', default=16,
		 	help="Number of collision steps")
parser.add_option("-r", "--retrySteps", dest="retrySteps", type='int', default=16,
		 	help="Number of retry steps for RTM")
(options, args) = parser.parse_args()

if len(args) != 0:
	parser.error("Incorrect number of args")

numThreads = options.numThreads
numIters = options.numIters
retrySteps = options.retrySteps
collisionSteps = options.collisionSteps

if not numThreads:
	numThreads = 2

if not numIters:
	if numThreads == 2:
		numIters = 8000000
	elif numThreads == 4:
		numIters = 4000000

experimen


citer = 0
while (citer <= collisionSteps) 
	collisionFactor = 1 << citer
	++citer
	exeCmd = assembleCommand(syncName, numThreads, numIters, sharedState, collisionFactor)	
	rRunTime   = 0
	rTxStarts  = 0
	rTxCommits = 0
	rTxAborts  = 0
	for ei in (0, experimentIters):
		results    = runOneExperiment(exeCmd)
		rRunTime   += results['RunTime']
		rTxStarts  += results['TxStarts']
		rTxCommits += results['TxCommits']
		rTxAborts  += results['TxAborts']

	aRunTime   = rRunTime   / experimentIters
	aTxStarts  = rTxStarts  / experimentIters
	aTxCommits = rTxCommits / experimentIters
	aTxAborts  = rTxAborts  / experimentIters

	print "Config-> ", syncName, ":", collisionFactor, ":", 
	print "RunTime    |", results['RunTime']
	print "TxStarts   |", results['TxStarts']
	print "TxCommits  |", results['TxCommits']
	print "TxAborts   |", results['TxAborts']

sys.exit(0)	
