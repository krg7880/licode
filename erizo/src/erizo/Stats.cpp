/*
 * Stats.cpp
 *
 */

#include <sstream>

#include "Stats.h"
#include "WebRtcConnection.h"

namespace erizo {

  DEFINE_LOGGER(Stats, "Stats");
  Stats::~Stats(){
    if (runningStats_){
      runningStats_ = false;
      statsThread_.join();
      ELOG_DEBUG("Stopped periodic stats report");
    }
  }
  void Stats::processRtcpPacket(char* buf, int length) {
    boost::mutex::scoped_lock lock(mapMutex_);    
    char* movingBuf = buf;
    int rtcpLength = 0;
    int totalLength = 0;
    do{
      movingBuf+=rtcpLength;
      RtcpHeader *chead= reinterpret_cast<RtcpHeader*>(movingBuf);
      rtcpLength= (ntohs(chead->length)+1)*4;      
      totalLength+= rtcpLength;
      this->processRtcpPacket(chead);
    } while(totalLength<length);
  }
  
  void Stats::processRtcpPacket(RtcpHeader* chead) {    
    unsigned int ssrc = chead->getSSRC();
    
//    ELOG_DEBUG("RTCP Packet: PT %d, SSRC %u,  block count %d ",chead->packettype,chead->getSSRC(), chead->getBlockCount()); 
    if (chead->packettype == RTCP_Receiver_PT){
      setFractionLost (chead->getFractionLost(), ssrc);
      setPacketsLost (chead->getLostPackets(), ssrc);
      setJitter (chead->getJitter(), ssrc);
      setSourceSSRC(chead->getSourceSSRC(), ssrc);
    }else if (chead->packettype == RTCP_Sender_PT){
      setRtcpPacketSent(chead->getPacketsSent(), ssrc);
      setRtcpBytesSent(chead->getOctetsSent(), ssrc);
    }else{
//      ELOG_DEBUG("REMB packet mantissa %u, exp %u", chead->getBrMantis(), chead->getBrExp());
//      ELOG_DEBUG("Packet not RR or SR going through stats %d", chead->packettype);
    }
  }
 
  std::string Stats::getStats() {
    boost::mutex::scoped_lock lock(mapMutex_);
    std::ostringstream theString;
    theString << "[";
    for (fullStatsMap_t::iterator itssrc=theStats_.begin(); itssrc!=theStats_.end();){
      unsigned long int currentSSRC = itssrc->first;
      theString << "{\"ssrc\":\"" << currentSSRC << "\",\n";
      if (currentSSRC == videoSSRC_){
        theString << "\"type\":\"" << "video\",\n";
      }else if (currentSSRC == audioSSRC_){
        theString << "\"type\":\"" << "audio\",\n";
      }
      for (singleSSRCstatsMap_t::iterator it=theStats_[currentSSRC].begin(); it!=theStats_[currentSSRC].end();){
        theString << "\"" << it->first << "\":\"" << it->second << "\"";
        if (++it != theStats_[currentSSRC].end()){
          theString << ",\n";
        }          
      }
      theString << "}";
      if (++itssrc != theStats_.end()){
        theString << ",";
      }
    }
    theString << "]";
    return theString.str(); 
  }

  void Stats::setPeriodicStats(int intervalMillis, WebRtcConnectionStatsListener* listener) {
    if (!runningStats_){
      theListener_ = listener;
      iterationsPerTick_ = static_cast<int>((intervalMillis*1000)/SLEEP_INTERVAL_);
      runningStats_ = true;
      ELOG_DEBUG("Starting periodic stats report with interval %d, iterationsPerTick %d", intervalMillis, iterationsPerTick_);
      statsThread_ = boost::thread(&Stats::sendStats, this);
    }else{
      ELOG_ERROR("Stats already started");
    }
  }

  void Stats::sendStats() {
    while(runningStats_) {
      if (++currentIterations_ >= (iterationsPerTick_)){
        theListener_->notifyStats(this->getStats());

        currentIterations_ =0;
      }
      usleep(SLEEP_INTERVAL_);
    }
  }
}

