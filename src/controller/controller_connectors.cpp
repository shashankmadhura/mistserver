#include <stdio.h> // cout, cerr
#include <string> 
#include <cstring>   // strcpy
#include <sys/stat.h> //stat
#include <mist/json.h>
#include <mist/config.h>
#include <mist/procs.h>
#include <mist/timing.h>
#include <mist/tinythread.h>
#include <mist/defines.h>
#include "controller_storage.h"
#include "controller_connectors.h"

#include <iostream>
#include <unistd.h>


///\brief Holds everything unique to the controller.
namespace Controller {

  static std::map<long long, std::string> currentConnectors; ///<The currently running connectors.


  static inline std::string toConn(long long i){
    return std::string("Conn") + JSON::Value(i).asString();
  }

  ///\brief Checks if the binary mentioned in the protocol argument is currently active, if so, restarts it.
  ///\param protocol The protocol to check.
  void UpdateProtocol(std::string protocol){
    std::map<long long, std::string>::iterator iter;
    for (iter = currentConnectors.begin(); iter != currentConnectors.end(); iter++){
      if (iter->second.substr(0, protocol.size()) == protocol){
        Log("CONF", "Killing connector for update: " + iter->second);
        Util::Procs::Stop(toConn(iter->first));
      }
    }
  }
  
  static inline void builPipedPart(JSON::Value & p, char * argarr[], int & argnum, JSON::Value & argset){
    argset.forEachMember([&] (const std::string & name, const JSON::Value & val) -> bool {
      if (val.isMember("option")){
        if (p.isMember(name)){
          if (val.isMember("type")){
            if (val["type"].asStringRef() == "str" && !p[name].isString()){
              p[name] = p[name].asString();
            }
            if ((val["type"].asStringRef() == "uint" || val["type"].asStringRef() == "int") && !p[name].isInt()){
              p[name] = p[name].asString();
            }
          }
          if (p[name].asStringRef().size() > 0){
            argarr[argnum++] = (char*)(val["option"].c_str());
            argarr[argnum++] = (char*)(p[name].c_str());
          }
        }else{
          if (name == "debug"){
            static std::string debugLvlStr;
            debugLvlStr = JSON::Value((long long)Util::Config::printDebugLevel).asString();
            argarr[argnum++] = (char*)(val["option"].c_str());
            argarr[argnum++] = (char*)debugLvlStr.c_str();
          }
        }
      }
      return true;
    });
  }
  
  static inline void buildPipedArguments(JSON::Value & p, char * argarr[], JSON::Value & capabilities){
    int argnum = 0;
    static std::string tmparg;
    tmparg = Util::getMyPath() + std::string("MistOut") + p["connector"].asStringRef();
    struct stat buf;
    if (::stat(tmparg.c_str(), &buf) != 0){
      tmparg = Util::getMyPath() + std::string("MistConn") + p["connector"].asStringRef();
    }
    if (::stat(tmparg.c_str(), &buf) != 0){
      return;
    }
    argarr[argnum++] = (char*)tmparg.c_str();
    JSON::Value & pipedCapa = capabilities["connectors"][p["connector"].asStringRef()];
    if (pipedCapa.isMember("required")){builPipedPart(p, argarr, argnum, pipedCapa["required"]);}
    if (pipedCapa.isMember("optional")){builPipedPart(p, argarr, argnum, pipedCapa["optional"]);}
  }
  
  ///\brief Checks current protocol coguration, updates state of enabled connectors if neccesary.
  ///\param p An object containing all protocols.
  ///\param capabilities An object containing the detected capabilities.
  void CheckProtocols(JSON::Value & p, JSON::Value & capabilities){
    std::map<long long, std::string> new_connectors;
    std::map<long long, std::string>::iterator iter;

    // used for building args
    int zero = 0;
    int out = fileno(stdout);
    int err = fileno(stderr);
    char * argarr[15];	// approx max # of args (with a wide margin)
    int i;

    std::string tmp;
    p.forEachIndice([&] (const unsigned int counter, JSON::Value & val) -> bool {
      std::string prevOnline = val["online"].asString();
      #define connName val["connector"].asStringRef()
      if ( !val.isMember("connector") || connName == ""){
        val["online"] = "Missing connector name";
        return true;
      }
      
      if ( !capabilities["connectors"].isMember(connName)){
        val["online"] = "Not installed";
        if (val["online"].asString() != prevOnline){
          Log("WARN", connName + " connector is enabled but doesn't exist on system! Ignoring connector.");
        }
        return true;
      }
      
      #define connCapa capabilities["connectors"][connName]
      
      if (connCapa.isMember("socket")){
        val["online"] = "Enabled";
        return true;
      }
      
      if (connCapa.isMember("required")){
        bool gotAll = true;
        connCapa["required"].forEachMember([&] (const std::string & name, JSON::Value & opt) -> bool {
          if ( !val.isMember(name) || val[name].asStringRef().size() < 1){
            gotAll = false;
            val["online"] = "Invalid configuration";
            if (val["online"].asString() != prevOnline){
              Log("WARN", connName + " connector is missing required parameter " + name + "! Ignoring connector.");
            }
            return false;
          }
          return true;
        });
        if (!gotAll){return true;}
      }
      
      val.removeMember("online");
      /// \todo Check dependencies?
      
      new_connectors[counter] = val.toString();
      if (Util::Procs::isActive(toConn(counter))){
        val["online"] = 1;
      }else{
        val["online"] = 0;
      }
      return true;
    });

    //shut down deleted/changed connectors
    for (iter = currentConnectors.begin(); iter != currentConnectors.end(); iter++){
      if (new_connectors.count(iter->first) != 1 || new_connectors[iter->first] != iter->second){
        Log("CONF", "Stopping connector " + iter->second);
        Util::Procs::Stop(toConn(iter->first));
      }
    }

    //start up new/changed connectors
    for (iter = new_connectors.begin(); iter != new_connectors.end(); iter++){
      if (currentConnectors.count(iter->first) != 1 || currentConnectors[iter->first] != iter->second || !Util::Procs::isActive(toConn(iter->first))){
        Log("CONF", "Starting connector: " + iter->second);
        // clear out old args
        for (i=0; i<15; i++){argarr[i] = 0;}
        // get args for this connector
        buildPipedArguments(p[(long long unsigned)iter->first], (char **)&argarr, capabilities);
        // start piped w/ generated args
        Util::Procs::StartPiped(toConn(iter->first), argarr, &zero, &out, &err);//redirects output to out. Must make a new pipe, redirect std err
      }
    }

    //store new state
    currentConnectors = new_connectors;
  }

}
