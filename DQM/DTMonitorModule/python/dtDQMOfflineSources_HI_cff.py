import FWCore.ParameterSet.Config as cms

from DQM.DTMonitorModule.dtChamberEfficiencyHI_cfi import *
from DQM.DTMonitorModule.dtSegmentTask_cfi import *
from DQM.DTMonitorModule.dtDCSByLumiTask_cfi import *
from DQM.DTMonitorModule.dtRunConditionVar_cfi import *
dtSegmentAnalysisMonitor.detailedAnalysis = True
dtSegmentAnalysisMonitor.slideTimeBins = False
dtSegmentAnalysisMonitor.nLSTimeBin = 5

from DQM.DTMonitorModule.dtResolutionTask_cfi import *

dqmInfoDT = cms.EDAnalyzer("DQMEventInfo",
                         subSystemFolder = cms.untracked.string('DT')
                         )


dtDataIntegrityUnpacker = cms.EDProducer("DTUnpackingModule",
    dataType = cms.string('DDU'),
    inputLabel = cms.InputTag('rawDataCollector'),
    fedbyType = cms.bool(False),
    useStandardFEDid = cms.bool(False),
    dqmOnly = cms.bool(True),                       
    readOutParameters = cms.PSet(
        debug = cms.untracked.bool(False),
        rosParameters = cms.PSet(
            writeSC = cms.untracked.bool(True),
            readingDDU = cms.untracked.bool(True),
            performDataIntegrityMonitor = cms.untracked.bool(True),
            readDDUIDfromDDU = cms.untracked.bool(True),
            debug = cms.untracked.bool(False),
            localDAQ = cms.untracked.bool(False)
        ),
        localDAQ = cms.untracked.bool(False),
        performDataIntegrityMonitor = cms.untracked.bool(True)
    )
)

from DQM.DTMonitorModule.dtDataIntegrityTask_cfi import *
DTDataIntegrityTask.processingMode = "Offline"

from DQM.DTMonitorModule.dtTriggerEfficiencyTask_cfi import *

dtSources = cms.Sequence(dtDataIntegrityUnpacker  +
                         DTDataIntegrityTask +
                         dtDCSByLumiMonitor + 
                         dtRunConditionVar + 
                         dtSegmentAnalysisMonitor +
                         dtResolutionAnalysisMonitor +
                         dtEfficiencyMonitor +
                         dtTriggerEfficiencyMonitor +
                         dqmInfoDT)

import EventFilter.DTRawToDigi.dturosunpacker_cfi
_dturosunpacker = EventFilter.DTRawToDigi.dturosunpacker_cfi.dturosunpacker.clone()
from Configuration.Eras.Modifier_run2_DT_2018_cff import run2_DT_2018
run2_DT_2018.toReplaceWith(dtDataIntegrityUnpacker, _dturosunpacker)


