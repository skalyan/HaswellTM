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
		process.wait()
		print self.output
		return self
		
	@property
	def returncode(self):
		return self.failed
		
		exeCmd = assembleCommand(syncName, numThreads, numIters, sharedState, collisionFactor)	

def assembleCommand(syncName, numThreads, numIters, sharedState, collisionFactor, numRetries):
	if syncName == "rtm2":
		perfStr = "perf stat -e r1c9,r2c9 "
	elif syncName == "hle2":
		perfStr = "perf stat -e r1c8,r2c8 "
	else:
		perfStr = ""
	
	exeName = "./exe." + syncName	
	threads = " -t " + str(numThreads)
	iters   = " -i " + str(numIters)
	if sharedState:
		sharedState = " -s " + " 1"
	else:
		sharedState = " -s " + " 0"
	collisionFactor = " -c " + str(collisionFactor)
	retries = " -r " + str(numRetries)
	outFile         = " >& exper.log"

	exeStr = perfStr + exeName + threads + iters + sharedState + collisionFactor + retries + outFile
	return exeStr

def parseTxStats(syncName, logFileName):
	txS = 0; txC = 0; txA = 0
	if not logFileName:
		return (txT, txC, txA)
	if syncName == "rtm2":
		res = Command("grep \"1c9\" exper.log").run()
	else:
		res = Command("grep \"1c8\" exper.log").run()
	if res.output:
		ts = res.output.replace(',','')
		txS = (ts.split('r')[0])
	if syncName == "rtm2":
		res = Command("grep \"2c9\" exper.log").run()
	else:
		res = Command("grep \"2c8\" exper.log").run()
	if res.output:
		ts = res.output.replace(',','')
		txC = (ts.split('r')[0])
	return (txS, txC, txA)
		

def runOneExperiment(syncName, cmdStr):
	if not cmdStr:
		print "Error: experiment command is empty"

	print "Running ", cmdStr
		
	cres = Command(cmdStr).run()
	if cres.failed:
		print "Error: ", cres.error, "; Unable to run an experiment"

	logStartTime = ""
	powerOffTime = ""
	powerOnTime = ""
	guestOSOnTime = ""
	swapStartTime = ""
	swapEndTime = ""
	spinTime =  0
	runTime = 0.0
	while True:
		time.sleep(3)
		res = Command("grep \"finished in\" exper.log").run()
		if res.output:
			runTime = float(res.output.split('|')[1])
			TxStats = parseTxStats(syncName, "exper.log")
			break
		if (spinTime > 60):
			print "Error: timed out"
			break

	return {'RunTime' : runTime, 'TxStarts': TxStats[0], \
		'TxCommits' : TxStats[1], 'TxAborts' : 0}

def runOneSyncExperiment(syncName, numRetries):
	collisionFactor = 32
	if syncName == "psp2":
		collisionFactor = 4
	while (collisionFactor > 0):
		exeCmd = assembleCommand(syncName, numThreads, numIters, shareData, collisionFactor, numRetries)	
		rRunTime   = 0.0
		rTxStarts  = 0
		rTxCommits = 0
		rTxAborts  = 0
		for ei in (range(experimentIters)):
			results    = runOneExperiment(syncName, exeCmd)
			rRunTime   += float(results['RunTime'])
			rTxStarts  += int(results['TxStarts'])
			rTxCommits += int(results['TxCommits'])
			rTxAborts  += int(results['TxAborts'])
	
		aRunTime   = rRunTime   / experimentIters
		aTxStarts  = rTxStarts  / experimentIters
		aTxCommits = rTxCommits / experimentIters
		aTxAborts  = rTxAborts  / experimentIters
	
		print "Config-> ", syncName, ", ", collisionFactor , ", ", numRetries, ", ", aRunTime, ", ", aTxStarts, ", ", aTxCommits
		print "         RunTime    |", aRunTime
		print "         TxStarts   |", aTxStarts
		print "         TxCommits  |", aTxCommits
		print "         TxAborts   |", aTxAborts

		collisionFactor = collisionFactor >> 1

def runSyncType(syncName):
	if syncName == "rtm2":
		nRetries = 64
	else:
		nRetries = 0 
	
	while (nRetries >= 0):
		runOneSyncExperiment(syncName, nRetries)
		if nRetries == 0:
			break
		nRetries = nRetries / 2
		

parser = OptionParser()
parser.add_option("-t", "--numThreads", dest="numThreads", type='int',
			help="Number of threads")
parser.add_option("-i", "--numIters", dest="numIters", type='int',
			help="Number of iterations")
parser.add_option("-c", "--collSteps", dest="collisionSteps", type='int', default=16,
		 	help="Number of collision steps")
parser.add_option("-r", "--retrySteps", dest="retrySteps", type='int', default=16,
		 	help="Number of retry steps for RTM")
parser.add_option("-s", "--shareData", dest="shareData", type='int', default=16,
		 	help="Is the data shared")
(options, args) = parser.parse_args()

if len(args) != 0:
	parser.error("Incorrect number of args")

numThreads = options.numThreads
numIters = options.numIters
retrySteps = options.retrySteps
collisionSteps = options.collisionSteps
shareData = options.shareData

if not numThreads:
	numThreads = 2

if not numIters:
	if numThreads == 2:
		numIters = 80000000
	elif numThreads == 4:
		numIters = 40000000

experimentIters = 5

syncTypes = ['rtm2', 'hle2', 'nlk2', 'psp2']

for syncName in syncTypes:
	runSyncType(syncName)

sys.exit(0)	
