import os

debugmask = False
isutopia = False
isflood = False
errorrate = "1e-5"
port = 59144
#将下面这个地址改成你datalink.exe的地址
os.chdir("C:\\Users\\陈伟杰\\Desktop\\Lab1-2018(Win+Linux)\\Lab1-Windows-VS2017\\Debug")


logA = "-l" + ' ' + "A"
logB = "-l" + ' ' + "B"

for isutopia,isflood in [(i,j) for i in [False,True] for j in [False,True]]:
    commandA = "start" + ' ' + "datalink" + ' ' + "A"
    commandB = "start" + ' ' + "datalink" + ' ' + "B"
    logA = "-l" + ' ' + "A"
    logB = "-l" + ' ' + "B"

    commandA += ' ' + '-p' + str(port)
    commandB += ' ' + '-p' + str(port)
    port += 1
    if (debugmask):
        commandA += ' ' + "-d3"
        commandB += ' ' + "-d3"
    if (isutopia):
        commandA += ' ' + '-u'
        logA += '_utopia'
        commandB += ' ' + '-u'
        logB += '_utopia'
    if (isflood):
        commandA += ' ' + '-f'
        logA += '_flood'
        commandB += ' ' + '-f'
        logB += '_flood'
    if (errorrate != "1e-5"):
        commandA += ' ' + "-b" + errorrate
        logA += '_b' + errorrate
        commandB += ' ' + "-b" + errorrate
        logB += '_b' + errorrate
    logA += ".log"
    logB += ".log"
    os.system(commandA + ' ' + logA)
    os.system(commandB + ' ' + logB)
commandA = "start" + ' ' + "datalink" + ' ' + "A" + ' ' + '-p' + str(port) + " -f -b1e-4 " + "-l" + " A1e-4.log"
commandB = "start" + ' ' + "datalink" + ' ' + "B" + ' ' + '-p' + str(port) + " -f -b1e-4 " + "-l" + " B1e-4.log"
os.system(commandA)
os.system(commandB)
