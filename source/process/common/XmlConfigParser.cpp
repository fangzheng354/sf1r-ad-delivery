/**
 * @file XmlConfigParser.cpp
 * @brief Implements SF1Config class, which is a XML configuration file parser for SF-1 v5.0
 * @author MyungHyun (Kent)
 * @date 2008-09-05
 */

#include "XmlConfigParser.h"
#include "XmlSchema.h"
#include <util/ustring/UString.h>
#include <net/distribute/Util.h>

#include <common/type_defs.h>
#include <common/SFLogger.h>
#include <la-manager/LAPool.h>
#include <boost/filesystem.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/tokenizer.hpp>
#include <boost/shared_ptr.hpp>

#include <glog/logging.h>

#include <iostream>
#include <sstream>

#include <boost/asio.hpp>
#include <configuration-manager/FuzzyNormalizerConfig.h>

using namespace std;
using namespace izenelib::util::ticpp;

namespace sf1r
{

//------------------------- HELPER FUNCTIONS -------------------------
void downCase(std::string & str)
{
    for (string::iterator it = str.begin(); it != str.end(); it++)
    {
        *it = tolower(*it);
    }
}

int parseTruth(const string & str)
{
    std::string temp = str;
    downCase(temp);

    if (temp == "y" || temp == "yes")
        return 1;
    else if (temp == "n" || temp == "no")
        return 0;
    else
        return -1;
}

void parseByComma(const string & str, vector<string> & subStrList)
{
    subStrList.clear();

    std::size_t startIndex=0, endIndex=0;

    while ((startIndex = str.find_first_not_of(" ,\t", startIndex)) != std::string::npos)
    {
        endIndex = str.find_first_of(" ,\t", startIndex + 1);

        if (endIndex == string::npos)
        {
            endIndex = str.length();
        }

        std::string substr = str.substr(startIndex, endIndex - startIndex);
        startIndex = endIndex + 1;

        subStrList.push_back(substr);
    }
}

// ------------------------- HELPER MEMBER FUNCTIONS of XmlConfigParser -------------------------

ticpp::Element * XmlConfigParser::getUniqChildElement(
        const ticpp::Element * ele, const std::string & name, bool throwIfNoElement) const
{
    ticpp::Element * temp = NULL;
    temp = ele->FirstChildElement(name, false);

    if (!temp)
    {
        if (throwIfNoElement)
            throw_NoElement(name);
        else
            return NULL;
    }

    if (temp->NextSibling(name, false))
    {
        throw_MultipleElement(name);
    }

    return temp;
}

inline bool XmlConfigParser::getAttribute(
        const ticpp::Element * ele,
        const std::string & name,
        std::string & val,
        bool throwIfNoAttribute) const
{
    val = ele->GetAttribute(name);

    if (val.empty())
    {
        if (throwIfNoAttribute)
            throw_NoAttribute(ele, name);
        else
            return false;
    }

    return true;
}

bool XmlConfigParser::getAttribute(
        const ticpp::Element * ele,
        const std::string & name,
        bool & val,
        bool throwIfNoAttribute) const
{
    std::string temp;

    if (!getAttribute(ele, name, temp, throwIfNoAttribute))
        return false;

    switch(parseTruth(temp))
    {
        case 1:
            val = true;
            break;
        case 0:
            val = false;
            break;
        case -1:
            throw_TypeMismatch(ele, name, temp);
            break;
    }

    return true;
}

izenelib::util::UString::EncodingType XmlConfigParser::parseEncodingType(const std::string& encoding)
{
    izenelib::util::UString::EncodingType eType = izenelib::util::UString::UTF_8;
    if (encoding == "utf-8" || encoding == "utf8")
        eType = izenelib::util::UString::UTF_8;
    else if (encoding == "euc-kr" || encoding == "euckr")
        eType = izenelib::util::UString::EUC_KR;
    else if (encoding == "cp949")
        eType = izenelib::util::UString::CP949;
    else if (encoding == "euc-jp" || encoding == "eucjp")
        eType = izenelib::util::UString::EUC_JP;
    else if (encoding == "sjis")
        eType = izenelib::util::UString::SJIS;
    else if (encoding == "gb2312")
        eType = izenelib::util::UString::GB2312;
    else if (encoding == "big5")
        eType = izenelib::util::UString::BIG5;
    else if (encoding == "iso8859-15")
        eType = izenelib::util::UString::ISO8859_15;
    return eType;
}

// ------------------------- SF1Config-------------------------

SF1Config::SF1Config()
{
}

SF1Config::~SF1Config()
{
}

bool SF1Config::parseConfigFile(const string & fileName) throw(XmlConfigParserException)
{
    namespace bf=boost::filesystem;

    try
    {
        if (!boost::filesystem::exists(fileName))
        {
            std::cerr << "[SF1Config] Config File doesn't exist." << std::endl;
            return false;
        }

        /** schema validate begin */
        bf::path config_file(fileName);
        bf::path config_dir = config_file.parent_path();
        bf::path schema_file = config_dir/"schema"/"sf1config.xsd";
        std::string schema_file_string = schema_file.string();
        std::cout<<"XML Schema File: "<<schema_file_string<<std::endl;
        if (!boost::filesystem::exists(schema_file_string))
        {
            std::cerr << "[SF1Config] Schema File doesn't exist." << std::endl;
            return false;
        }

        XmlSchema schema(schema_file_string);
        bool schema_valid = schema.validate(fileName);
        std::list<std::string> schema_warning = schema.getSchemaValidityWarnings();
        if (schema_warning.size()>0)
        {
            std::list<std::string>::iterator it = schema_warning.begin();
            while (it!= schema_warning.end())
            {
                std::cout<<"[Schema-Waring] "<<*it<<std::endl;
                it++;
            }
        }
        if (!schema_valid)
        {
            //output schema errors
            std::list<std::string> schema_error = schema.getSchemaValidityErrors();
            if (schema_error.size()>0)
            {
                std::list<std::string>::iterator it = schema_error.begin();
                while (it!= schema_error.end())
                {
                    std::cerr<<"[Schema-Error] "<<*it<<std::endl;
                    it++;
                }
            }
            return false;
        }
        /** schema validate end */

        ticpp::Document configDocument(fileName.c_str());
        configDocument.LoadFile();

        // make sure the top level element is "SF1Config"; if it isn't, an exception is thrown
        Element * sf1config = NULL;
        if ((sf1config = configDocument.FirstChildElement("SF1Config", false)) == NULL)
        {
            throw_NoElement("SF1Config");
        }

        parseSystemSettings(getUniqChildElement(sf1config, "System"));
        parseDeploymentSettings(getUniqChildElement(sf1config, "Deployment"));
    }
    catch (ticpp::Exception err)
    {
        size_t substart = err.m_details.find("\nDescription: ");
        substart = substart + strlen("\nDescription: ");

        string msg = err.m_details.substr(substart);
        size_t pos = 0;

        while ((pos = msg.find("\n", pos)) != string::npos)
        {
            msg.replace(pos, 1, " ");
            pos++;
        }

        msg.insert(0, "Exception occured while parsing file: ");

        throw XmlConfigParserException(msg);
    }

    return true;
} // END - SF1Config::parseConfigFile()

// 1. SYSTEM SETTINGS  -------------------------------------

void SF1Config::parseSystemSettings(const ticpp::Element * system)
{
    //get resource dir
    getAttribute(getUniqChildElement(system, "Resource"), "path", resource_dir_);

    getAttribute(getUniqChildElement(system, "WorkingDir"), "path", working_dir_);

    getAttribute(getUniqChildElement(system, "LogConnection"), "str", log_conn_str_);

    getAttribute(getUniqChildElement(system, "LogServerConnection"), "host", logServerConnectionConfig_.host);
    getAttribute(getUniqChildElement(system, "LogServerConnection"), "rpcport", logServerConnectionConfig_.rpcPort);
    getAttribute(getUniqChildElement(system, "LogServerConnection"), "rpc_thread_num", logServerConnectionConfig_.rpcThreadNum, false);
    getAttribute(getUniqChildElement(system, "LogServerConnection"), "driverport", logServerConnectionConfig_.driverPort, false);

    parseBundlesDefault(getUniqChildElement(system, "BundlesDefault"));

    parseFirewall(getUniqChildElement(system, "Firewall"));

    parseTokenizer(getUniqChildElement(system, "Tokenizing"));

    parseLanguageAnalyzer(getUniqChildElement(system, "LanguageAnalyzer"));
}

void SF1Config::parseBundlesDefault(const ticpp::Element * bundles)
{
    Element * bundle = NULL;
    bundle = getUniqChildElement(bundles, "IndexBundle");
    defaultIndexBundleParam_.LoadXML(getUniqChildElement(bundle, "Parameter"), false);
    bundle = getUniqChildElement(bundles, "MiningBundle");
    defaultMiningBundleParam_.LoadXML(getUniqChildElement(bundle, "Parameter"), false);
}

void SF1Config::parseFirewall(const ticpp::Element * fireElement)
{
    Iterator<Element> allow_it("Allow");
    for (allow_it = allow_it.begin(fireElement); allow_it != allow_it.end(); allow_it++)
    {
        string ipAddress;

        getAttribute(allow_it.Get(), "value", ipAddress, false);

        if (!ipAddress.empty())
            firewallConfig_.allowIPList_.push_back(ipAddress);
        else
            throw XmlConfigParserException("Firewall - \"Allow\" : Need to provide value.");
    }

    Iterator<Element> deny_it("Deny");
    for (deny_it = deny_it.begin(fireElement); deny_it != deny_it.end(); deny_it++)
    {
        string ipAddress;


        getAttribute(deny_it.Get(), "value", ipAddress, false);

        if (!ipAddress.empty())
            firewallConfig_.denyIPList_.push_back(ipAddress);
        else
            throw XmlConfigParserException("Firewall - \"Deny\" : Need to provide value.");
    }
}

void SF1Config::parseTokenizer(const ticpp::Element * tokenizing)
{
    string id, method, value, code;

    Iterator<Element> tokenizer_it("Tokenizer");
    for (tokenizer_it = tokenizer_it.begin(tokenizing); tokenizer_it != tokenizer_it.end(); tokenizer_it++)
    {
        getAttribute(tokenizer_it.Get(), "id", id);
        getAttribute(tokenizer_it.Get(), "method", method);
        getAttribute(tokenizer_it.Get(), "value", value, false);
        getAttribute(tokenizer_it.Get(), "code", code, false);

        downCase(method);

        if (!validateID(id)) throw_TypeMismatch(tokenizer_it.Get(), "id", id, "Alphabets, Numbers, Dot(.), Dash(-) and Underscore(_)");

        if (method != "allow" && method != "divide" && method != "unite")
        {
            stringstream message;
            message << "<Tokenizer> \"" << id << "\": method must one of 'allow', 'divide', or 'unite'.";
            throw XmlConfigParserException(message.str());
        }

        // check if both "value" and "code" are empty
        if (value.empty() && code.empty())
        {
            stringstream message;
            message << "<Tokenizer> \"" << id << "\": Need to provide either \"value\" or \"code\".";
            throw XmlConfigParserException(message.str());
        }

        TokenizerConfigUnit tkUnit(id, method, value, code);

        if (!tokenizerConfigNameMap_.insert(pair<string, TokenizerConfigUnit>(id, tkUnit)).second)
        {
            throw XmlConfigParserException("Duplicate <Tokenizer> IDs");
        }

        laManagerConfig_.addTokenizerConfig(tkUnit);
    }
}

void SF1Config::parseLanguageAnalyzer(const ticpp::Element * languageAnalyzer)
{
    // 1. <LanguageAnalyzer>
    laDictionaryPath_ = "";
    string dictionaryPath;

    getAttribute(languageAnalyzer, "dictionarypath", dictionaryPath);

    unsigned int updateDictInterval;

    getAttribute(languageAnalyzer, "updatedictinterval", updateDictInterval);

    laManagerConfig_.updateDictInterval_ = updateDictInterval;
    std::cout<<"set update LA dictionary interval: "<<updateDictInterval<<std::endl;

    // 2. <Method>

    // common settings among analyzers
    string id;
    string analysis;

    Iterator<Element> analyzer_it("Method");
    for (analyzer_it = analyzer_it.begin(languageAnalyzer); analyzer_it != analyzer_it.end(); analyzer_it++)
    {
        getAttribute(analyzer_it.Get(), "id", id);
        getAttribute(analyzer_it.Get(), "analysis", analysis);

        downCase(analysis);

        if (!validateID(id)) throw_TypeMismatch(analyzer_it.Get(), "id", id, "Alphabets, Numbers, Dot(.), Dash(-) and Underscore(_)");

        // 1.1. Initializes the container unit
        LAConfigUnit laUnit(id, analysis);

        bool bCaseSensitive = false;
        if (getAttribute(analyzer_it.Get(), "casesensitive", bCaseSensitive, false))
        {
            laUnit.setCaseSensitive(bCaseSensitive);
        }

        for (int flagIdx = 0; flagIdx < 2; ++flagIdx)
        {
            string flagName = flagIdx == 0 ? "idxflag" : "schflag";
            string flagVal;
            if (!getAttribute(analyzer_it.Get(), flagName, flagVal, false))
                continue;
            downCase(flagVal);

            unsigned char flagCharVal;
            if (flagVal == "none")
            {
                flagCharVal = 0x0;
            }
            else if (flagVal == "prime")
            {
                flagCharVal = 0x01;
            }
            else if (flagVal == "second")
            {
                flagCharVal = 0x02;
            }
            else if (flagVal == "all")
            {
                flagCharVal = 0x03;
            }
            else
            {
                stringstream message;
                message << "\"" << id << "\": "<<flagName<<"'s value should be one of none/prime/second/all.";
                throw XmlConfigParserException(message.str());
                continue;
            }
            if (flagIdx == 0)
                laUnit.setIdxFlag(flagCharVal);
            else
                laUnit.setSchFlag(flagCharVal);
        }

        // set configurations by different analyzer types

        if (analysis == "token")
        {
        }
        else if (analysis == "char")
        {
            string advOption;
            getAttribute(analyzer_it.Get(), "advoption", advOption, false);
            laUnit.setAdvOption(advOption);
        }
        else if (analysis == "ngram")
        {
            unsigned int minLevel = 0, maxLevel = 0, maxNo = 0;
            bool bApart = false;

            getAttribute(analyzer_it.Get(), "min", minLevel);
            getAttribute(analyzer_it.Get(), "max", maxLevel);
            getAttribute(analyzer_it.Get(), "maxno", maxNo);
            getAttribute(analyzer_it.Get(), "apart", bApart);

            if (minLevel < 0) throw_TypeMismatch(analyzer_it.Get(), "min", minLevel, "Positive integer");
            if (maxLevel < 0) throw_TypeMismatch(analyzer_it.Get(), "max", maxLevel, "Positive integer");
            if (maxNo < 0) throw_TypeMismatch(analyzer_it.Get(), "maxno", maxNo, "Positive integer");
            if (maxLevel < minLevel) throw_TypeMismatch(analyzer_it.Get(), "min, max", 0, "Max level must be greater than equals to Min level");

            laUnit.setMinLevel(minLevel);
            laUnit.setMaxLevel(maxLevel);
            laUnit.setMaxNo(maxNo);
            laUnit.setApart(bApart);
        }
        else if (analysis == "matrix")
        {
            bool bPrefix=false, bSuffix=false;

            getAttribute(analyzer_it.Get(), "prefix", bPrefix);
            getAttribute(analyzer_it.Get(), "suffix", bSuffix);

            laUnit.setPrefix(bPrefix);
            laUnit.setSuffix(bSuffix);
        }
        else if (analysis == "english"
#ifdef USE_WISEKMA
                || analysis == "korean"
#endif
#ifdef USE_IZENECMA
                || analysis == "chinese"
#endif
#ifdef USE_IZENEJMA
                || analysis == "japanese"
#endif
                )
        {
            Element * settings = NULL;
            // dictionary path set here can overwrite the global one set in <LanguageAnalyzer>
            string mode, option, specialChar, dictionaryPath_inner;

            settings = getUniqChildElement(analyzer_it.Get(), "settings", false);
            if (settings)
            {
                getAttribute(settings, "mode", mode, false);
                getAttribute(settings, "option", option, false);
                getAttribute(settings, "specialchar", specialChar, false);
                getAttribute(settings, "dictionarypath", dictionaryPath_inner, false);

                if (!dictionaryPath_inner.empty())
                {
                    laDictionaryPath_ = dictionaryPath_inner;
                }

                downCase(mode);
                downCase(option);

                if (mode != "all" && mode != "noun" && mode != "label") throw_TypeMismatch(settings, "mode", mode, "\"all\", \"noun\" or \"label\"");

                laUnit.setMode(mode);
                laUnit.setOption(option);
                laUnit.setSpecialChar(specialChar);

                // TODO: always set global dictionary path first
                if (!dictionaryPath_inner.empty())
                {
                    laUnit.setDictionaryPath(dictionaryPath_inner);
                    if (analysis == "chinese")
                    {
                        LAPool::getInstance()->set_cma_path(dictionaryPath_inner);
                        std::cout<<"set_cma_path : "<<dictionaryPath_inner<<std::endl;
                    }
                    else if (analysis == "japanese")
                    {
                        LAPool::getInstance()->set_jma_path(dictionaryPath_inner);
                        std::cout<<"set_jma_path : "<<dictionaryPath_inner<<std::endl;
                    }
                }
                else if (!dictionaryPath.empty() && dictionaryPath_inner.empty())
                {
                    laUnit.setDictionaryPath(dictionaryPath);
                    if (analysis == "chinese")
                    {
                        LAPool::getInstance()->set_cma_path(dictionaryPath);
                        std::cout<<"set_cma_path : "<<dictionaryPath<<std::endl;
                    }
                    else if (analysis == "japanese")
                    {
                        LAPool::getInstance()->set_jma_path(dictionaryPath_inner);
                        std::cout<<"set_jma_path : "<<dictionaryPath_inner<<std::endl;
                    }
                }
                else
                {
                    stringstream message;
                    message << "\"" << id << "\": Need to provide a dictionary path";
                    throw XmlConfigParserException(message.str());
                }
            }
        }
        else if (analysis == "multilang")
        {
            string advOption;
            getAttribute(analyzer_it.Get(), "advoption", advOption);
            laUnit.setAdvOption(advOption);

            bool stem = true;
            if (getAttribute(analyzer_it.Get(), "stem", stem, false))
            {
                laUnit.setStem(stem);
            }

            bool lower = true;
            if (getAttribute(analyzer_it.Get(), "lower", lower, false))
            {
                laUnit.setLower(lower);
            }

            Element * settings = NULL;
            string mode;

            settings = getUniqChildElement(analyzer_it.Get(), "settings", false);
            if (settings)
            {
                getAttribute(settings, "mode", mode, false);
                downCase(mode);

                if (mode != "all" && mode != "noun" && mode != "label") throw_TypeMismatch(settings, "mode", mode, "\"all\", \"noun\" or \"label\"");

                laUnit.setMode(mode);

            }
        }
        else
        {
            stringstream message;
            message << "\"" << id << "\": Wrong analysis name. Should be one of "
#ifdef USE_IZENECMA
                << "\"chinese\", "
#endif
#ifdef USE_IZENEJMA
                << "\"japanese\", "
#endif
#ifdef USE_WISEKMA
                << "\"korean\", "
#endif
                << "\"token\", \"char\", \"ngram\", \"matrix\", or \"english\". (currently:"
                << analysis << ")";
            throw XmlConfigParserException(message.str());
        }

        if (!laConfigIdNameMap_.insert(pair<string, LAConfigUnit>(id, laUnit)).second)
        {
            throw XmlConfigParserException("Duplicate LanguageAnalyzer Method IDs");
        }

        laManagerConfig_.addLAConfig(laUnit);

    }// for all the <Method> elements. (the LAConfigUnits)

} // END - SF1Config::parseLanguageAnalyzer()

void SF1Config::parseDeploymentSettings(const ticpp::Element * deploy)
{
    parseBrokerAgent(getUniqChildElement(deploy, "BrokerAgent"));

    parseDistributedCommon(getUniqChildElement(deploy, "DistributedCommon"));
    parseDistributedTopology(getUniqChildElement(deploy, "DistributedTopology")); // Make sure "DistributedCommon" parsed first.
    parseDistributedUtil(getUniqChildElement(deploy, "DistributedUtil"));
    parseAdCommonConfig(getUniqChildElement(deploy, "AdConfig", false));
}

void SF1Config::parseBrokerAgent(const ticpp::Element * brokerAgent)
{
    getAttribute(brokerAgent, "usecache", brokerAgentConfig_.useCache_, false);
    getAttribute(brokerAgent, "enabletest", brokerAgentConfig_.enableTest_,false);
    getAttribute(brokerAgent, "threadnum", brokerAgentConfig_.threadNum_,false);
    getAttribute(brokerAgent, "port", brokerAgentConfig_.port_,false);
}

void SF1Config::parseDistributedCommon(const ticpp::Element * distributedCommon)
{
    getAttribute(distributedCommon, "clusterid", distributedCommonConfig_.clusterId_);
    getAttribute(distributedCommon, "username", distributedCommonConfig_.userName_);
    getAttribute(distributedCommon, "workerport", distributedCommonConfig_.workerPort_);
    getAttribute(distributedCommon, "masterport", distributedCommonConfig_.masterPort_);
    getAttribute(distributedCommon, "datarecvport", distributedCommonConfig_.dataRecvPort_);
    getAttribute(distributedCommon, "filesyncport", distributedCommonConfig_.filesync_rpcport_);

    distributedCommonConfig_.check_level_ = 2;
    getAttribute(distributedCommon, "check_level_", distributedCommonConfig_.check_level_, false);
    distributedCommonConfig_.baPort_ = brokerAgentConfig_.port_;

    if (!net::distribute::Util::getLocalHostIp(distributedCommonConfig_.localHost_))
    {
        getAttribute(distributedCommon, "localhost", distributedCommonConfig_.localHost_);
        std::cout << "failed to detect local host ip, set by config: " << distributedCommonConfig_.localHost_ << std::endl;
    }
    else
        std::cout << "local host ip : " << distributedCommonConfig_.localHost_ << std::endl;

    std::cout << "cluster id : " << distributedCommonConfig_.clusterId_ << std::endl;
    std::cout << "check level : " << distributedCommonConfig_.check_level_ << std::endl;
}

void SF1Config::parseDistributedTopology(const ticpp::Element * topology)
{
    if (topology)
    {
        if (distributedCommonConfig_.clusterId_.empty())
        {
            throw std::runtime_error("Cluster Id for \"DistributedTopology\" is empty, "
                                     "make sure \"DistributedCommon\" has been parsed first.");
        }

        getAttribute(topology, "enable", topologyConfig_.enabled_);

        Sf1rTopology& sf1rTopology = topologyConfig_.sf1rTopology_;
        sf1rTopology.clusterId_ = distributedCommonConfig_.clusterId_;
        //getAttribute(topology, "nodenum", sf1rTopology.nodeNum_);

        Sf1rNode& sf1rNode = sf1rTopology.curNode_;
        sf1rNode.userName_ = distributedCommonConfig_.userName_;
        sf1rNode.host_ = distributedCommonConfig_.localHost_;
        sf1rNode.baPort_ = distributedCommonConfig_.baPort_;
        sf1rNode.dataPort_ = distributedCommonConfig_.dataRecvPort_;

        ticpp::Element * sf1rNodeElem = getUniqChildElement(topology, "CurrentSf1rNode");
        getAttribute(sf1rNodeElem, "replicaid", sf1rNode.replicaId_);
        uint32_t nodeid;
        getAttribute(sf1rNodeElem, "nodeid", nodeid);
        sf1rNode.nodeId_ = (shardid_t)nodeid;

        Sf1rNodeMaster& sf1rNodeMaster = sf1rNode.master_;
        sf1rNodeMaster.port_ = distributedCommonConfig_.masterPort_;
        parseNodeMaster(getUniqChildElement(sf1rNodeElem, "MasterServer", false), sf1rNodeMaster);

        Sf1rNodeWorker& sf1rNodeWorker = sf1rNode.worker_;
        sf1rNodeWorker.port_ = distributedCommonConfig_.workerPort_;
        parseNodeWorker(getUniqChildElement(sf1rNodeElem, "WorkerServer", false), sf1rNodeWorker);

        //std::cout << topologyConfig.toString() << std::endl;
    }
}

void SF1Config::parseNodeMaster(const ticpp::Element * master, Sf1rNodeMaster& sf1rNodeMaster)
{
    if (master)
    {
        getAttribute(master, "enable", sf1rNodeMaster.enabled_);
        getAttribute(master, "name", sf1rNodeMaster.name_);
        //getAttribute(master, "shardnum", sf1rNodeMaster.totalShardNum_);

        //Iterator<Element> service_it("DistributedService");
        //for (service_it = service_it.begin(master); service_it != service_it.end(); service_it++)
        //{
        //    parseServiceMaster(service_it.Get(), sf1rNodeMaster);
        //}
    }
}

void SF1Config::parseNodeWorker(const ticpp::Element * worker, Sf1rNodeWorker& sf1rNodeWorker)
{
    if (worker)
    {
        getAttribute(worker, "enable", sf1rNodeWorker.enabled_);

        //Iterator<Element> service_it("DistributedService");
        //for (service_it = service_it.begin(worker); service_it != service_it.end(); service_it++)
        //{
        //    parseServiceWorker(service_it.Get(), sf1rNodeWorker);
        //}
   }
}

void SF1Config::addServiceMaster(const std::string& serviceName, const MasterCollection& masterCollection)
{
    bool ret = topologyConfig_.sf1rTopology_.addServiceMaster(serviceName, masterCollection);
    if (!ret)
        std::cerr << "failed add service master for collection: " << masterCollection.name_;
}

void SF1Config::removeServiceMaster(const std::string& service, const std::string& coll)
{
    topologyConfig_.sf1rTopology_.removeServiceMaster(service, coll);
}

void SF1Config::addServiceWorker(const std::string& service, const std::string& coll)
{
    topologyConfig_.sf1rTopology_.addServiceWorker(service, coll);
}

void SF1Config::removeServiceWorker(const std::string& service, const std::string& coll)
{
    topologyConfig_.sf1rTopology_.removeServiceWorker(service, coll);
}

//void SF1Config::parseServiceWorker(const ticpp::Element * service, Sf1rNodeWorker& sf1rNodeWorker)
//{
//    WorkerServiceInfo service_info;
//    getAttribute(service, "type", service_info.serviceName_);
//    Iterator<Element> collection_it("Collection");
//    for (collection_it = collection_it.begin(service); collection_it != collection_it.end(); collection_it++)
//    {
//        std::string collection;
//        getAttribute(collection_it.Get(), "name", collection);
//        downCase(collection);
//        service_info.collectionList_.push_back(collection);
//    }
//    sf1rNodeWorker.workerServices_[service_info.serviceName_] = service_info;
//}

void SF1Config::parseDistributedUtil(const ticpp::Element * distributedUtil)
{
    // ZooKeeper configuration
    ticpp::Element* zk = getUniqChildElement(distributedUtil, "ZooKeeper");
    getAttribute(zk, "disable", distributedUtilConfig_.zkConfig_.disabled_);
    getAttribute(zk, "servers", distributedUtilConfig_.zkConfig_.zkHosts_);
    getAttribute(zk, "sessiontimeout", distributedUtilConfig_.zkConfig_.zkRecvTimeout_);

    // Distributed File System configuration
    ticpp::Element* dfs = getUniqChildElement(distributedUtil, "DFS");
    getAttribute(dfs, "type", distributedUtilConfig_.dfsConfig_.type_, false);
    getAttribute(dfs, "supportfuse", distributedUtilConfig_.dfsConfig_.isSupportFuse_, false);
    getAttribute(dfs, "mountdir", distributedUtilConfig_.dfsConfig_.mountDir_, false);
    getAttribute(dfs, "server", distributedUtilConfig_.dfsConfig_.server_, false);
    getAttribute(dfs, "port", distributedUtilConfig_.dfsConfig_.port_, false);
}

void SF1Config::parseAdCommonConfig(const ticpp::Element * adconfig_ele)
{
    if (!adconfig_ele)
        return;
    getAttribute(adconfig_ele, "enable", adconfig_.is_enabled);
    if (adconfig_.is_enabled)
    {
        ticpp::Element* dmp_ele = getUniqChildElement(adconfig_ele, "AdDMPServer");
        getAttribute(dmp_ele, "ip", adconfig_.dmp_ip);
        getAttribute_IntType(dmp_ele, "port", adconfig_.dmp_port);
        ticpp::Element* stream_ele = getUniqChildElement(adconfig_ele, "AdStreamServer");
        getAttribute(stream_ele, "ip", adconfig_.stream_log_ip);
        getAttribute_IntType(stream_ele, "port", adconfig_.stream_log_port);
    }
}

// ------------------------- CollectionConfig-------------------------
CollectionConfig::CollectionConfig()
{
}

CollectionConfig::~CollectionConfig()
{
}

bool CollectionConfig::parseConfigFile(const string& collectionName ,const string & fileName, CollectionMeta& collectionMeta) throw(XmlConfigParserException)
{
    namespace bf=boost::filesystem;

    try
    {
        if (!boost::filesystem::exists(fileName))
        {
            std::cerr << "[SF1Config] Config File doesn't exist." << std::endl;
            return false;
        }

        /** schema validate begin */
        bf::path config_file(fileName);
        bf::path config_dir = config_file.parent_path();
        bf::path schema_file = config_dir/"schema"/"collection.xsd";
        std::string schema_file_string = schema_file.string();
        std::cout<<"XML Schema File: "<<schema_file_string<<std::endl;
        if (!boost::filesystem::exists(schema_file_string))
        {
            std::cerr << "[SF1Config] Schema File doesn't exist." << std::endl;
            return false;
        }
        XmlSchema schema(schema_file_string);
        bool schema_valid = schema.validate(fileName);
        std::list<std::string> schema_warning = schema.getSchemaValidityWarnings();
        if (schema_warning.size()>0)
        {
            std::list<std::string>::iterator it = schema_warning.begin();
            while (it != schema_warning.end())
            {
                std::cout<<"[Schema-Waring] "<<*it<<std::endl;
                it++;
            }
        }
        if (!schema_valid)
        {
            //output schema errors
            std::list<std::string> schema_error = schema.getSchemaValidityErrors();
            if (schema_error.size()>0)
            {
                std::list<std::string>::iterator it = schema_error.begin();
                while (it != schema_error.end())
                {
                    std::cerr<<"[Schema-Error] "<<*it<<std::endl;
                    it++;
                }
            }
            return false;
        }
        /** schema validate end */

        ticpp::Document configDocument(fileName.c_str());
        configDocument.LoadFile();

        // make sure the top level element is "SF1Config"; if it isn't, an exception is thrown
        Element * collection = NULL;
        if ((collection = configDocument.FirstChildElement("Collection", false)) == NULL)
        {
            throw_NoElement("Collection");
        }

        collectionMeta.setName(collectionName);
        parseCollectionSettings(collection, collectionMeta);
    }
    catch (ticpp::Exception err)
    {
        size_t substart = err.m_details.find("\nDescription: ");
        substart = substart + strlen("\nDescription: ");

        string msg = err.m_details.substr(substart);
        size_t pos = 0;

        while ((pos = msg.find("\n", pos)) != string::npos)
        {
            msg.replace(pos, 1, " ");
            pos++;
        }

        msg.insert(0, "Exception occured while parsing file: ");

        throw XmlConfigParserException(msg);
    }

    return true;
}

void CollectionConfig::parseCollectionSettings(const ticpp::Element * collection, CollectionMeta & collectionMeta)
{
    parseCollectionPath(getUniqChildElement(collection, "Path"), collectionMeta);

    parseCollectionSchema(getUniqChildElement(collection, "DocumentSchema"), collectionMeta);

    // ACL
    Iterator<Element> aclIterator("ACL");
    for (aclIterator = aclIterator.begin(collection);
         aclIterator != aclIterator.end(); ++aclIterator)
    {
        std::string aclAllow;
        std::string aclDeny;
        getAttribute(aclIterator.Get(), "allow", aclAllow, false);
        getAttribute(aclIterator.Get(), "deny", aclDeny, false);

        collectionMeta.aclAllow(aclAllow);
        collectionMeta.aclDeny(aclDeny);
    }

    // IndexBundle
    Element* indexBundle = getUniqChildElement(collection, "IndexBundle", false);
    if (indexBundle)
    {
        parseIndexBundleParam(getUniqChildElement(indexBundle, "Parameter", false), collectionMeta);
        parseIndexBundleSchema(getUniqChildElement(indexBundle, "Schema", false), collectionMeta);
        parseIndexShardSchema(getUniqChildElement(indexBundle, "ShardSchema", false), collectionMeta); //after Schema
        parseZambeziNode(getUniqChildElement(indexBundle, "ZambeziSchema", false), collectionMeta);

        IndexBundleConfiguration& indexBundleConfig = *collectionMeta.indexBundleConfig_;
        const std::string& collectionName = collectionMeta.getName();
        indexBundleConfig.isMasterAggregator_ = SF1Config::get()->checkSearchMasterAggregator(collectionName);
        indexBundleConfig.isWorkerNode_ = SF1Config::get()->checkSearchWorker(collectionName);
    }

    // MiningBundle
    Element* miningBundle = getUniqChildElement(collection, "MiningBundle" , false);
    if (miningBundle)
    {
        collectionMeta.miningBundleConfig_->isMasterAggregator_ = collectionMeta.indexBundleConfig_->isMasterAggregator_;

        Element* miningSchema = getUniqChildElement(miningBundle, "Schema", false);
        parseMiningBundleSchema(miningSchema, collectionMeta);

        Element* miningParam = getUniqChildElement(miningBundle, "Parameter", false);
        parseMiningBundleParam(miningParam, collectionMeta);
    }
}

void CollectionConfig::parseCollectionPath(const ticpp::Element * path, CollectionMeta & collectionMeta)
{
    CollectionPath collPath;
    string basepath;

    getAttribute(path, "basepath", basepath);

    Element * specificPath = NULL;
    string subPath;

    collPath.setBasePath(basepath);

    specificPath = getUniqChildElement(path, "SCD", false);
    if (specificPath)
    {
        getAttribute(specificPath, "path", subPath, false);
        collPath.setScdPath(subPath);
    }

    specificPath = getUniqChildElement(path, "CollectionData", false);
    if (specificPath)
    {
        getAttribute(specificPath, "path", subPath, false);
        collPath.setCollectionDataPath(subPath);
    }

    specificPath = getUniqChildElement(path, "Query", false);
    if (specificPath)
    {
        getAttribute(specificPath, "path", subPath, false);
        collPath.setQueryDataPath(subPath);
    }

    collectionMeta.setCollectionPath(collPath);
    collectionMeta.indexBundleConfig_->collPath_ = collPath;
    collectionMeta.miningBundleConfig_->collPath_ = collPath;
}

void CollectionConfig::parseCollectionSchema(const ticpp::Element * documentSchema, CollectionMeta & collectionMeta)
{
    //used to check duplicate property names
    set<string> propertyNameList;

    Iterator<Element> property("Property");

    bool hasDate = false;

    for (property = property.begin(documentSchema); property != property.end(); property++)
    {
        try
        {
            //holds all the configuration data of this property
            PropertyConfigBase propertyConfig;

            string propertyName, type;
            PropertyDataType dataType = UNKNOWN_DATA_PROPERTY_TYPE;

            getAttribute(property.Get(), "name", propertyName);
            getAttribute(property.Get(), "type", type);

            downCase(type);

            if (!validateID(propertyName))
                throw XmlConfigParserException("Alphabets, Numbers, Dot(.), Dash(-) and Underscore(_)");
            if (!((propertyNameList.insert(propertyName)).second))
            {
                throw XmlConfigParserException("Duplicate property names");
            }

            // find right data type
            if (type == "string")
            {
                dataType = STRING_PROPERTY_TYPE;
            }
            else if (type == "subdoc")
            {
                dataType = SUBDOC_PROPERTY_TYPE;
            }
            else if (type == "int8")
            {
                dataType = INT8_PROPERTY_TYPE;
            }
            else if (type == "int16")
            {
                dataType = INT16_PROPERTY_TYPE;
            }
            else if (type == "int32")
            {
                dataType = INT32_PROPERTY_TYPE;
            }
            else if (type == "int64")
            {
                dataType = INT64_PROPERTY_TYPE;
            }
            else if (type == "float")
            {
                dataType = FLOAT_PROPERTY_TYPE;
            }
            else if (type == "double")
            {
                dataType = DOUBLE_PROPERTY_TYPE;
            }
            else if (type == "datetime")
            {
                dataType = DATETIME_PROPERTY_TYPE;
            }
            else
            {
                throw_TypeMismatch(property.Get(), "name", type, "\"string\", \"subdoc\", \"int\" or \"float\"");
            }
            propertyConfig.propertyName_ = propertyName;
            propertyConfig.propertyType_ = dataType;
            collectionMeta.insertProperty(propertyConfig);

            boost::to_lower(propertyName);
            if (propertyName == "date")
                hasDate = true;
        }
        catch (XmlConfigParserException & e)
        {
            throw e;
        }
    } //property iteration

    if (!hasDate)
    {
        PropertyConfigBase propertyConfig;
        propertyConfig.propertyName_ = "DATE";
        propertyConfig.propertyType_ = DATETIME_PROPERTY_TYPE;
        collectionMeta.insertProperty(propertyConfig);
    }

    ///we set property Id here
    collectionMeta.numberProperty();
}

void CollectionConfig::parseIndexBundleParam(const ticpp::Element * index, CollectionMeta & collectionMeta)
{
    SF1Config* sf1Config = SF1Config::get();
    CollectionParameterConfig params(sf1Config->defaultIndexBundleParam_);
    if (index) params.LoadXML(index, true);

    std::string encoding;

    params.GetString("Sia/encoding", encoding);
    downCase(encoding);
    IndexBundleConfiguration& indexBundleConfig = *collectionMeta.indexBundleConfig_;
    std::string searchAnalyzer;
    params.GetString("Sia/searchanalyzer", searchAnalyzer);
    if (!searchAnalyzer.empty())
    {
        if ((sf1Config->laConfigIdNameMap_.find(searchAnalyzer)) == sf1Config->laConfigIdNameMap_.end())
        {
            throw XmlConfigParserException("Undefined analyzer configuration id, " + searchAnalyzer);
        }

        /// TODO, add a hidden alias here
        AnalysisInfo analysisInfo;
        analysisInfo.analyzerId_ = searchAnalyzer;
        sf1Config->analysisPairList_.insert(analysisInfo);
    }

    indexBundleConfig.searchAnalyzer_ = searchAnalyzer;
    indexBundleConfig.encoding_ = parseEncodingType(encoding);
    params.GetString("Sia/wildcardtype", indexBundleConfig.wildcardType_, "unigram");
    params.Get("Sia/indexunigramproperty", indexBundleConfig.bIndexUnigramProperty_);
    params.Get("Sia/unigramsearchmode", indexBundleConfig.bUnigramSearchMode_);
    std::string multilangGranualarity;
    params.GetString("Sia/multilanggranularity", multilangGranualarity, "field");
    indexBundleConfig.setIndexMultiLangGranularity(multilangGranualarity);

    ///TODO for ranking
    ///ranking is not configed right now, although we keep such interface here
    RankingConfigUnit& rankingUnit = indexBundleConfig.rankingManagerConfig_.rankingConfigUnit_;
    TextRankingType e_qd_text_model = RankingType::BM25;
    rankingUnit.setTextRankingModel(e_qd_text_model);

    //Index strategy
    izenelib::ir::indexmanager::IndexManagerConfig& indexmanager_config = indexBundleConfig.indexConfig_;

    int64_t memorypool = 0;
    params.Get<int64_t>("IndexStrategy/memorypoolsize", memorypool);
    indexmanager_config.indexStrategy_.memory_ = memorypool;
    indexmanager_config.indexStrategy_.indexDocLength_ = false;
    indexmanager_config.indexStrategy_.skipInterval_ = 8;
    indexmanager_config.indexStrategy_.maxSkipLevel_ = 3;
    indexmanager_config.storeStrategy_.param_ = "file";
    std::string indexLevel;
    params.GetString("IndexStrategy/indexlevel", indexLevel, "wordlevel");
    indexmanager_config.indexStrategy_.indexLevel_ = (indexLevel == "wordlevel")?
        izenelib::ir::indexmanager::WORDLEVEL : izenelib::ir::indexmanager::DOCLEVEL;

    std::string samplePolicy;
    params.GetString("IndexStrategy/samplepolicy", samplePolicy, "false");
    indexmanager_config.indexStrategy_.samplePolicy_ = (samplePolicy == "true")?
        izenelib::ir::indexmanager::WITHSAMPLE : izenelib::ir::indexmanager::NOSAMPLE;

    params.GetString("IndexStrategy/indexpolicy", indexmanager_config.indexStrategy_.indexMode_, "default");
    std::string indexMergePolicy;
    params.GetString("IndexStrategy/mergepolicy",indexMergePolicy, "file");
    if (indexMergePolicy == "memory")
        indexmanager_config.mergeStrategy_.requireIntermediateFileForMerging_ = false;
    else
        indexmanager_config.mergeStrategy_.requireIntermediateFileForMerging_ = true;
    indexmanager_config.mergeStrategy_.param_ = "dbt";
    params.GetString("IndexStrategy/cron", indexBundleConfig.cronIndexer_, "");
    //indexmanager_config.indexStrategy_.optimizeSchedule_ = indexBundleConfig.cronIndexer_;
    std::set<std::string> directories;
    params.Get("CollectionDataDirectory", directories);
    params.Get("IndexStrategy/logcreateddoc", indexBundleConfig.logCreatedDoc_);
    params.Get("IndexStrategy/autorebuild", indexBundleConfig.isAutoRebuild_);
    params.Get("IndexStrategy/indexdoclength", indexmanager_config.indexStrategy_.indexDocLength_);

    if (!directories.empty())
    {
        indexmanager_config.indexStrategy_.indexLocations_.assign(directories.begin(), directories.end());
        indexBundleConfig.collectionDataDirectories_.assign(directories.begin(), directories.end());
    }

    params.Get("Sia/triggerqa", indexBundleConfig.bTriggerQA_);
    params.Get("Sia/enable_parallel_searching", indexBundleConfig.enable_parallel_searching_);
    params.Get("Sia/enable_forceget_doc", indexBundleConfig.enable_forceget_doc_);
    params.Get<std::size_t>("Sia/doccachenum", indexBundleConfig.documentCacheNum_);
    params.Get<std::size_t>("Sia/searchcachenum", indexBundleConfig.searchCacheNum_);
    params.Get("Sia/refreshsearchcache", indexBundleConfig.refreshSearchCache_);
    params.Get<time_t>("Sia/refreshcacheinterval", indexBundleConfig.refreshCacheInterval_);
    params.Get<std::size_t>("Sia/filtercachenum", indexBundleConfig.filterCacheNum_);
    params.Get<std::size_t>("Sia/mastersearchcachenum", indexBundleConfig.masterSearchCacheNum_);
    params.Get<std::size_t>("Sia/topknum", indexBundleConfig.topKNum_);
    params.Get<std::size_t>("Sia/sortcacheupdateinterval", indexBundleConfig.sortCacheUpdateInterval_);
    params.GetString("LanguageIdentifier/dbpath", indexBundleConfig.languageIdentifierDbPath_, "");

    LAPool::getInstance()->setLangIdDbPath(indexBundleConfig.languageIdentifierDbPath_);

    indexBundleConfig.localHostUsername_ = sf1Config->distributedCommonConfig_.userName_;
    indexBundleConfig.localHostIp_ = sf1Config->distributedCommonConfig_.localHost_;
}

void CollectionConfig::parseIndexShardSchema(const ticpp::Element * shardSchema, CollectionMeta & collectionMeta)
{
    if (!shardSchema)
        return;

    if (!SF1Config::get()->topologyConfig_.enabled_)
    {
        throw XmlConfigParserException("ShardSchema found but the distribute is disabled!");
    }

    IndexBundleConfiguration& indexBundleConfig = *(collectionMeta.indexBundleConfig_);

    Iterator<Element> keyElem("ShardKey");
    for (keyElem = keyElem.begin(shardSchema); keyElem != keyElem.end(); keyElem++)
    {
        std::string key;
        getAttribute(keyElem.Get(), "name", key);
        indexBundleConfig.indexShardKeys_.push_back(key);
    }

    if (SF1Config::get()->isMasterEnabled() || SF1Config::get()->isWorkerEnabled())
    {
        Iterator<Element> service_it("DistributedService");
        for (service_it = service_it.begin(shardSchema); service_it != service_it.end(); service_it++)
        {
            parseServiceMaster(service_it.Get(), collectionMeta);
        }
        if (!SF1Config::get()->topologyConfig_.sf1rTopology_.curNode_.master_.hasAnyService() &&
            !SF1Config::get()->topologyConfig_.sf1rTopology_.curNode_.worker_.hasAnyService())
        {
            throw XmlConfigParserException("no distributed service configured while master/worker is enabled.");
        }
    }
}

void CollectionConfig::parseServiceMaster(const ticpp::Element * service, CollectionMeta& collectionMeta)
{
    std::string service_type;
    getAttribute(service, "type", service_type, true);
    MasterCollection masterCollection;
    masterCollection.name_ = collectionMeta.getName();

    if (SF1Config::get()->isWorkerEnabled())
    {
        SF1Config::get()->addServiceWorker(service_type, masterCollection.name_);
    }

    Sf1rTopology& sf1rTopology = SF1Config::get()->topologyConfig_.sf1rTopology_;

    std::string shardids;
    if (getAttribute(service, "shardids", shardids, false))
    {
        boost::char_separator<char> sep(", ");
        boost::tokenizer<boost::char_separator<char> > tokens(shardids, sep);

        boost::tokenizer<boost::char_separator<char> >::iterator it;
        std::set<shardid_t> shardid_set;
        for (it = tokens.begin(); it != tokens.end(); ++it)
        {
            uint32_t shardid;
            try
            {
                shardid = boost::lexical_cast<uint32_t>(*it);
                if (shardid < 1)
                {
                    std::stringstream ss;
                    ss << "invalid shardid \"" << shardid << "\" in <MasterServer ...> <Collection name=\""
                        << masterCollection.name_ << "\" shardids=\"" << shardids << "\"";
                    throw std::runtime_error(ss.str());
                }
            }
            catch (const std::exception& e)
            {
                throw std::runtime_error(
                    std::string("failed to parse shardids: ") + shardids + ", " + e.what());
            }
            if (shardid == sf1rTopology.curNode_.nodeId_ &&
                !SF1Config::get()->isWorkerEnabled())
            {
                throw XmlConfigParserException("The shard id include myself, but myself worker is disabled.");
            }
            shardid_set.insert((shardid_t)shardid);
        }
        masterCollection.shardList_.insert(masterCollection.shardList_.end(),
            shardid_set.begin(), shardid_set.end());
    }

    if (masterCollection.shardList_.empty())
    {
        if (!SF1Config::get()->isWorkerEnabled())
        {
            throw XmlConfigParserException("no sharding id configured and current node is not a worker");
        }
        // set to current as default
        masterCollection.shardList_.push_back(sf1rTopology.curNode_.nodeId_);
    }

    if (service_type == Sf1rTopology::getServiceName(Sf1rTopology::SearchService))
    {
        IndexBundleConfiguration& indexBundleConfig = *(collectionMeta.indexBundleConfig_);
        indexBundleConfig.col_shard_info_ = masterCollection;
    }
    else
    {
        throw XmlConfigParserException("Unsupported distributed service type: " + service_type);
    }

    if (!SF1Config::get()->isMasterEnabled())
        return;

    SF1Config::get()->addServiceMaster(service_type, masterCollection);
}


void CollectionConfig::parseIndexBundleSchema(const ticpp::Element * indexSchemaNode, CollectionMeta & collectionMeta)
{
    if (!indexSchemaNode)
        return;

    IndexBundleConfiguration& indexBundleConfig = *(collectionMeta.indexBundleConfig_);

    indexBundleConfig.isSchemaEnable_ = true;
    indexBundleConfig.isNormalSchemaEnable_ = true;
    indexBundleConfig.setSchema(collectionMeta.documentSchema_);

    Iterator<Element> property("Property");

    for (property = property.begin(indexSchemaNode); property != property.end(); property++)
    {
        try
        {
            parseIndexSchemaProperty(property.Get(), collectionMeta);
        }
        catch (XmlConfigParserException & e)
        {
            throw e;
        }
    }
    const IndexBundleSchema& indexSchema = collectionMeta.indexBundleConfig_->indexSchema_;

    Iterator<Element> virtualproperty("VirtualProperty");
    for (virtualproperty = virtualproperty.begin(indexSchemaNode); virtualproperty != virtualproperty.end(); virtualproperty++)
    {
        try
        {
            string propertyName;
            getAttribute(virtualproperty.Get(), "name", propertyName);

            string pName = propertyName;
            boost::to_lower(pName);

            PropertyConfig p;
            p.setName(propertyName);

            Iterator<Element> subproperty("SubProperty");
            for (subproperty = subproperty.begin(virtualproperty.Get()); subproperty != subproperty.end(); subproperty++)
            {
                string subPropName;
                getAttribute(subproperty.Get(), "name", subPropName);
                p.subProperties_.push_back(subPropName);
                PropertyConfig tmp;
                tmp.setName(subPropName);
                IndexBundleSchema::const_iterator pit = indexSchema.find(tmp);
                if (pit != indexSchema.end())
                {
                    ///Ugly here, each property right now should have same analysisinfo
                    p.setAnalysisInfo(pit->getAnalysisInfo());
                }
            }

            p.setOriginalName(propertyName);
            p.setIsIndex(true);
            collectionMeta.indexBundleConfig_->indexSchema_.insert(p);
        }
        catch (XmlConfigParserException & e)
        {
            throw e;
        }
    }


    ///we update property Id here
    ///It is because that IndexBundle might add properties "xx_unigram", in this case, we must keep the propertyId consistent
    ///between IndexBundle and other bundles
    ///collectionMeta.numberProperty();
    indexBundleConfig.numberProperty();

    // map<property name, weight>
    std::map<string, float>& propertyWeightMap = collectionMeta.indexBundleConfig_->rankingManagerConfig_.propertyWeightMapByProperty_;

    for (IndexBundleSchema::const_iterator it = indexSchema.begin();
        it != indexSchema.end(); ++it)
    {
        if (it->getRankWeight() >= 0.0f)
        {
            propertyWeightMap[it->getName()] = it->getRankWeight();
        }
        if (! it->subProperties_.empty())
        {
            float weight = 0.0f;
            for (unsigned i = 0; i < it->subProperties_.size(); ++i)
            {
                PropertyConfig p;
                p.setName(it->subProperties_[i]);

                IndexBundleSchema::const_iterator pit = indexSchema.find(p);
                if ((pit != indexSchema.end())&&(pit->getRankWeight() >= 0.0f))
                {
                    weight += pit->getRankWeight();
                }
            }
            propertyWeightMap[it->getName()] = weight;
        }
    }
}

void CollectionConfig::parseMiningBundleParam(const ticpp::Element * mining, CollectionMeta & collectionMeta)
{
    CollectionParameterConfig params(SF1Config::get()->defaultMiningBundleParam_);
    if (mining) params.LoadXML(mining, true);
    // PARSING MINING CONFIG
    MiningConfig& mining_config = collectionMeta.miningBundleConfig_->mining_config_;
    //for mining
    params.Get<uint32_t>("DocumentMiningPara/docnumlimit", mining_config.dcmin_param.docnum_limit);
    params.GetString("DocumentMiningPara/cron", mining_config.dcmin_param.cron);

    // for product ranking
    params.GetString("ProductRankingPara/cron",
                     mining_config.product_ranking_param.cron);

    // for mining task
    params.Get<std::size_t>("MiningTaskPara/threadnum",
                            mining_config.mining_task_param.threadNum);
    
    //for laser
    params.GetString("LaserPara/modelType", mining_config.laser_param.modelType);
    boost::trim(mining_config.laser_param.modelType);
    params.GetString("LaserPara/kvaddr", mining_config.laser_param.kvaddr);
    params.Get<int>("LaserPara/kvport", mining_config.laser_param.kvport);
    params.GetString("LaserPara/mqaddr", mining_config.laser_param.mqaddr);
    params.Get<int>("LaserPara/mqport", mining_config.laser_param.mqport);
    LOG(INFO)<<mining_config.laser_param.modelType;

    std::set<std::string> directories;
    params.Get("CollectionDataDirectory", directories);

    if (!directories.empty())
    {
        collectionMeta.miningBundleConfig_->collectionDataDirectories_.assign(directories.begin(), directories.end());
    }

    static bool FirstRun = true;
    if (FirstRun)
    {
        FirstRun = false;

        MiningBundleConfiguration::system_resource_path_ = SF1Config::get()->getResourceDir();
        MiningBundleConfiguration::system_working_path_ = SF1Config::get()->getWorkingDir();
    }
}

void CollectionConfig::parseMiningBundleSchema(const ticpp::Element * mining_schema_node, CollectionMeta & collectionMeta)
{
    if (!mining_schema_node)
        return;

    //** PARSE MINING SCHEMA BEGIN
    MiningBundleConfiguration& miningBundleConfig = *(collectionMeta.miningBundleConfig_);
    miningBundleConfig.documentSchema_ = collectionMeta.documentSchema_;
    miningBundleConfig.isSchemaEnable_ = true;

    MiningSchema& mining_schema = miningBundleConfig.mining_schema_;
    PropertyDataType property_type;

    ticpp::Element * task_node = getUniqChildElement(mining_schema_node, "Group", false);
    std::string property_name;

    mining_schema.group_enable = false;
    if (task_node)
    {
        Iterator<Element> it("Property");
        const IndexBundleSchema& indexSchema = collectionMeta.indexBundleConfig_->indexSchema_;

        for (it = it.begin(task_node); it != it.end(); ++it)
        {
            const ticpp::Element* propNode = it.Get();
            getAttribute(propNode, "name", property_name);
            bool gottype = collectionMeta.getPropertyType(property_name, property_type);
            if (!gottype)
            {
                throw XmlConfigParserException("The type of property ["+property_name+"] in <Group> is unknown.");
            }

            GroupConfig groupConfig(property_type);
            getAttribute(propNode, "rebuild", groupConfig.isConfigAsRebuild, false);

            PropertyConfig propConfig;
            propConfig.setName(property_name);
            IndexBundleSchema::const_iterator propIt = indexSchema.find(propConfig);
            bool isRTypeNumeric = false;
            if (propIt != indexSchema.end())
            {
                groupConfig.isRTypeStr = propIt->isRTypeString();
                isRTypeNumeric = propIt->isRTypeNumeric();
            }

            if (groupConfig.isNumericType())
            {
                if (!isRTypeNumeric)
                {
                    throw XmlConfigParserException("As property ["+property_name+"] in <Group> is int or float type, "
                                                   "it needs to be configured as a filter property like below:\n"
                                                   "<IndexBundle> <Schema> <Property name=\"Price\"> <Indexing filter=\"yes\" ...");
                }
            }
            else if (!groupConfig.isStringType() &&
                     !groupConfig.isDateTimeType())
            {
                throw XmlConfigParserException("Property ["+property_name+"] in <Group> is not string, int, float or datetime type.");
            }

            mining_schema.group_config_map[property_name] = groupConfig;

            LOG(INFO) << "group property: " << property_name
                      << ", type: " << property_type
                      << ", isRebuild: " << groupConfig.isRebuild();
        }
        mining_schema.group_enable = true;
    }

    task_node = getUniqChildElement(mining_schema_node, "Attr", false);
    mining_schema.attr_enable = false;
    if (task_node)
    {
        int propNum = 0;
        Iterator<Element> propIt("Property");
        for (propIt = propIt.begin(task_node); propIt != propIt.end(); ++propIt)
        {
            if (++propNum > 1)
            {
                throw XmlConfigParserException("in <Attr> Config, at most one <Property> is allowed.");
            }

            getAttribute(propIt.Get(), "name", property_name);
            bool gottype = collectionMeta.getPropertyType(property_name, property_type);
            if (!gottype || property_type != STRING_PROPERTY_TYPE)
            {
                throw XmlConfigParserException("<Property> ["+property_name+"] in <Attr> is not string type.");
            }
            mining_schema.attr_property.propName = property_name;
            mining_schema.attr_enable = true;
        }

        Iterator<Element> excludeIt("Exclude");
        std::set<std::string,AttrConfig::caseInSensitiveLess >& excludeAttrNames = mining_schema.attr_property.excludeAttrNames;
        for (excludeIt = excludeIt.begin(task_node); excludeIt != excludeIt.end(); ++excludeIt)
        {
            std::string attrName;
            getAttribute(excludeIt.Get(), "name", attrName);
            excludeAttrNames.insert(attrName);
        }
    }

    task_node = getUniqChildElement(mining_schema_node, "ProductRanking", false);
    parseProductRankingNode(task_node, collectionMeta);


    task_node = getUniqChildElement(mining_schema_node, "SuffixMatch", false);
    mining_schema.suffixmatch_schema.suffix_match_enable = false;
    if (task_node)
    {
        Iterator<Element> it("Property");
        for (it = it.begin(task_node); it != it.end(); ++it)
        {
            getAttribute(it.Get(), "name", property_name);
            bool gottype = collectionMeta.getPropertyType(property_name, property_type);
            if (!gottype || property_type != STRING_PROPERTY_TYPE)
            {
                throw XmlConfigParserException("Property ["+property_name+"] used in SuffixMatch is not string type.");
            }
            mining_schema.suffixmatch_schema.suffix_match_properties.push_back(property_name);
            mining_schema.suffixmatch_schema.suffix_match_enable = true;

            PropertyConfig property;
            property.setName(property_name);
            IndexBundleSchema::iterator sp = collectionMeta.indexBundleConfig_->indexSchema_.find(property);
            if ( collectionMeta.indexBundleConfig_->indexSchema_.end() != sp)
            {
                PropertyConfig p(*sp);
                p.bSuffixIndex_ = true;
                collectionMeta.indexBundleConfig_->indexSchema_.erase(sp);
                collectionMeta.indexBundleConfig_->indexSchema_.insert(p);
            }
            else
            {
                property.bSuffixIndex_ = true;
                collectionMeta.indexBundleConfig_->indexSchema_.insert(property);
            }
        }

        //
        Iterator<Element> itv("VirtualProperty");
        for (itv = itv.begin(task_node); itv != itv.end(); ++itv)
        {
            //SubProperty 
            bool hasSubPropery = false;
            Iterator<Element> subproperty("SubProperty");
            for (subproperty = subproperty.begin(itv.Get()); subproperty != subproperty.end(); subproperty++)
            {
                std::string subPropName;
                getAttribute(subproperty.Get(), "name", subPropName);

                PropertyConfigBase tmpConfig;
                tmpConfig.propertyName_ = subPropName;
                PropertyDataType dataType = UNKNOWN_DATA_PROPERTY_TYPE;
                dataType = collectionMeta.documentSchema_.find(tmpConfig)->propertyType_;

                if (dataType != STRING_PROPERTY_TYPE)
                {
                    throw XmlConfigParserException("VirtualProperty's subproperty only support STRING type!!!");
                }
                mining_schema.suffixmatch_schema.virtual_property.virtual_properties.push_back(subPropName);
                hasSubPropery = true;
            }
            if (!hasSubPropery)
            {
                throw XmlConfigParserException("VirtualProperty's subproperty is empty!!!");
            }

            //Name
            std::string property_name;
            getAttribute(itv.Get(), "name", property_name);
            mining_schema.suffixmatch_schema.virtual_property.virtualName = property_name;
            mining_schema.suffixmatch_schema.suffix_match_properties.push_back(property_name);
            mining_schema.suffixmatch_schema.suffix_match_enable = true;
        }

        std::sort(mining_schema.suffixmatch_schema.suffix_match_properties.begin(), mining_schema.suffixmatch_schema.suffix_match_properties.end());

        ticpp::Element* subNode = getUniqChildElement(task_node, "TokenizeDictionary", true);
        if (subNode)
        {
            getAttribute(subNode, "path", property_name);
            mining_schema.suffixmatch_schema.suffix_match_tokenize_dicpath = property_name;
        }
        else
        {
            throw XmlConfigParserException("["+property_name+"] used in SuffixMatch is missing.");
        }

        ticpp::Element* subNodeNorm = getUniqChildElement(task_node, "Normalizer", false);
        parseFuzzyNormalizerNode(
            subNodeNorm, mining_schema.suffixmatch_schema.normalizer_config);

        ticpp::Element* subNodeTopK = getUniqChildElement(task_node, "GroupCounterTopK", false);
        if (subNodeTopK)
        {
            int32_t topk = 100000;
            getAttribute(subNodeTopK, "num", topk);
            mining_schema.suffixmatch_schema.suffix_groupcounter_topk = topk;
        }

        Iterator<Element> filterit("FilterProperty");
        const IndexBundleSchema& indexSchema = collectionMeta.indexBundleConfig_->indexSchema_;
        for (filterit = filterit.begin(task_node); filterit != filterit.end(); ++filterit)
        {
            const ticpp::Element* propNode = filterit.Get();
            getAttribute(propNode, "name", property_name);
            bool gottype = collectionMeta.getPropertyType(property_name, property_type);
            if (!gottype)
            {
                throw XmlConfigParserException("The type of property ["+property_name+"] in <SuffixMatch> is unknown.");
            }

            std::string type;
            getAttribute(propNode, "filtertype", type);

            int32_t amplifier = 1;
            getAttribute(propNode, "amplifier", amplifier, false);

            bool searchable = false;
            getAttribute(propNode, "searchable", searchable, false);

            if (type == "group")
            {
                GroupConfigMap::const_iterator cit = mining_schema.group_config_map.find(property_name);
                if (cit == mining_schema.group_config_map.end())
                {
                    throw XmlConfigParserException("Property ["+property_name+"] in <SuffixMatch> must be one of item configured in <Group> if it has type group.");
                }
                mining_schema.suffixmatch_schema.group_filter_properties.push_back(property_name);
                if (searchable)
                {
                    if (property_type != STRING_PROPERTY_TYPE)
                        throw XmlConfigParserException("Property ["+property_name+"] in <SuffixMatch> searchable must be string type.");
                    mining_schema.suffixmatch_schema.searchable_properties.push_back(property_name);
                }
            }
            else if (type == "attribute")
            {
                if ( property_name != mining_schema.attr_property.propName)
                {
                    throw XmlConfigParserException("Property ["+property_name+"] in <SuffixMatch> must be one of item configured in <Attr> if it has type attribute.");
                }
                mining_schema.suffixmatch_schema.attr_filter_properties.push_back(property_name);
                if (searchable)
                {
                    if (property_type != STRING_PROPERTY_TYPE)
                        throw XmlConfigParserException("Property ["+property_name+"] in <SuffixMatch> searchable must be string type.");
                    mining_schema.suffixmatch_schema.searchable_properties.push_back(property_name);
                }
            }
            else if (type == "string")
            {
                if ( property_type != STRING_PROPERTY_TYPE)
                {
                    throw XmlConfigParserException("Property ["+property_name+"] in <SuffixMatch> is not string type.");
                }
                mining_schema.suffixmatch_schema.str_filter_properties.push_back(property_name);
            }
            else if (type == "date")
            {
                if ( property_type != DATETIME_PROPERTY_TYPE)
                {
                    throw XmlConfigParserException("Property ["+property_name+"] in <SuffixMatch> is not date type.");
                }
                mining_schema.suffixmatch_schema.date_filter_properties.push_back(property_name);
            }
            else if (type == "numeric")
            {
                PropertyConfig propConfig;
                propConfig.setName(property_name);
                IndexBundleSchema::const_iterator propIt = indexSchema.find(propConfig);

                NumericFilterConfig number_filterconfig(property_type);
                number_filterconfig.property = property_name;
                number_filterconfig.amplifier = amplifier;
                if (number_filterconfig.isNumericType())
                {
                    if (propIt == indexSchema.end() || !propIt->isIndex() || !propIt->getIsFilter())
                    {
                        throw XmlConfigParserException("As property ["+property_name+"] in <SuffixMatch> is numeric type, "
                            "it needs to be configured as a filter property like below:\n"
                            "<IndexBundle> <Schema> <Property name=\"Price\"> <Indexing filter=\"yes\" ...");
                    }
                    mining_schema.suffixmatch_schema.num_filter_properties.push_back(number_filterconfig);
                }
                else
                {
                    throw XmlConfigParserException("Property ["+property_name+"] in <SuffixMatch> is not numeric type.");
                }
            }
            else
            {
                throw XmlConfigParserException("Property ["+property_name+"] in <SuffixMatch> unknown filter type.");
            }
        }

        std::sort(mining_schema.suffixmatch_schema.group_filter_properties.begin(), mining_schema.suffixmatch_schema.group_filter_properties.end());
        std::sort(mining_schema.suffixmatch_schema.attr_filter_properties.begin(), mining_schema.suffixmatch_schema.attr_filter_properties.end());
        std::sort(mining_schema.suffixmatch_schema.str_filter_properties.begin(), mining_schema.suffixmatch_schema.str_filter_properties.end());
        std::sort(mining_schema.suffixmatch_schema.date_filter_properties.begin(), mining_schema.suffixmatch_schema.date_filter_properties.end());
        std::sort(mining_schema.suffixmatch_schema.num_filter_properties.begin(), mining_schema.suffixmatch_schema.num_filter_properties.end());
    }

    task_node = getUniqChildElement(mining_schema_node, "AdIndex", false);
    parseAdIndexNode(task_node, collectionMeta);
    
    task_node = getUniqChildElement(mining_schema_node, "Laser", false);
}

void CollectionConfig::parseFuzzyNormalizerNode(
    const ticpp::Element* normNode,
    FuzzyNormalizerConfig& normalizerConfig) const
{
    if (!normNode)
        return;

    std::string typeName;
    getAttribute(normNode, "padding", typeName);

    FuzzyNormalizerType typeId = normalizerConfig.getNormalizerType(typeName);
    if (typeId == FUZZY_NORMALIZER_NUM)
    {
        std::string error("unknown <Normalizer> padding value \"" +
                          typeName + "\"");
        throw XmlConfigParserException(error);
    }

    normalizerConfig.type = typeId;

    getAttribute_IntType(normNode, "max_index_token",
                         normalizerConfig.maxIndexToken, false);
}

void CollectionConfig::parseAdIndexNode(
        const ticpp::Element* adIndexNode,
        CollectionMeta& collectionMeta) const
{
    if(!adIndexNode)
        return;

    MiningSchema& miningSchema =
        collectionMeta.miningBundleConfig_->mining_schema_;


    AdIndexConfig& adIndexConfig = miningSchema.ad_index_config;

    getAttribute(adIndexNode, "enable_selector", adIndexConfig.enable_selector);
    getAttribute(adIndexNode, "enable_rec", adIndexConfig.enable_rec);
    getAttribute(adIndexNode, "enable_sponsored_search", adIndexConfig.enable_sponsored_search);
    getAttribute(adIndexNode, "adlog_topic", adIndexConfig.adlog_topic);
    miningSchema.ad_index_config.isEnable = true;
    adIndexConfig.ad_common_data_path = collectionMeta.getCollectionPath().getBasePath() + "/ad_common_data";
}
    
void CollectionConfig::parseZambeziNode(
    const ticpp::Element* zambeziNode,
    CollectionMeta& collectionMeta) const
{
    if (!zambeziNode)
        return;

    collectionMeta.indexBundleConfig_->setZambeziSchema(collectionMeta.documentSchema_);

    collectionMeta.indexBundleConfig_->isZambeziSchemaEnable_ = true;
    ZambeziConfig& zambeziConfig = collectionMeta.indexBundleConfig_->zambeziConfig_;

    getAttribute(zambeziNode, "reverse", zambeziConfig.reverse, false);
    getAttribute_ByteSize(zambeziNode, "poolSize", zambeziConfig.poolSize, true);
    getAttribute(zambeziNode, "poolCount", zambeziConfig.poolCount, true);

    std::string indexType;
    getAttribute(zambeziNode, "indexType", indexType, false);
    if (indexType.empty() || indexType == "ATTR")
        zambeziConfig.indexType_ = ZambeziIndexType::DefultIndexType;
    else if (indexType == "POSITION")
        zambeziConfig.indexType_ = ZambeziIndexType::PostionIndexType;
    else
    {
        stringstream message;
        message << "zambeziConfig indexType is wrong: default is AttrScoreIndex, or use indexType = \"ATTR\" / \"POSITION\"";
        throw XmlConfigParserException(message.str());
    }
	getAttribute_ByteSize(zambeziNode, "vocabSize", zambeziConfig.vocabSize, false);

    zambeziConfig.isEnable = true;
    //zambeziConfig.indexFilePath = collectionMeta.indexBundleConfig_->collPath_.getCollectionDataPath();
    zambeziConfig.system_resource_path_ = SF1Config::get()->getResourceDir();

    Iterator<Element> property("IndexProperty");
    for (property = property.begin(zambeziNode); property != property.end(); property++)
    {
        try
        {
            ZambeziProperty zProperty;
            PropertyStatus  sProperty;
            // name and type
            std::string propertyName;
            std::string type;
            getAttribute(property.Get(), "name", propertyName);
            if (!validateID(propertyName))
                throw XmlConfigParserException("Alphabets, Numbers, Dot(.), Dash(-) and Underscore(_)");

            PropertyConfigBase tmpConfig;
            tmpConfig.propertyName_ = propertyName;
            PropertyDataType dataType = UNKNOWN_DATA_PROPERTY_TYPE;
            dataType = collectionMeta.documentSchema_.find(tmpConfig)->propertyType_;

            zProperty.type = dataType;
            std::string pName = propertyName;
            boost::to_lower(pName);
            if ((pName == "date")||(pName == "docid"))
            {
                stringstream message;
                message << "DATE/DOCID are inherent properties and should not exist within IndexBundleSchema";
                throw XmlConfigParserException(message.str());
            }
            zProperty.name = propertyName;

            // tokenizer
            bool istokenizer;
            getAttribute(property.Get(), "tokenizer", istokenizer, true);
            zProperty.isTokenizer = istokenizer;
            sProperty.isTokenizer = istokenizer;

            // weight
            float propertyWeight;
            if (getAttribute_FloatType(property.Get(), "weight", propertyWeight, false))
            {
                zProperty.weight = propertyWeight;
                sProperty.weight = propertyWeight;
            }

            // poolSize
            uint32_t poolsize = 0;
            if (getAttribute_ByteSize(property.Get(), "poolSize", poolsize, false))
                zProperty.poolSize = poolsize;
            else
                zProperty.poolSize = zambeziConfig.poolSize;

            // filter
            bool isFilter;
            getAttribute(property.Get(), "filter", isFilter, true);
            zProperty.isFilter = isFilter;
            sProperty.isFilter = isFilter;
            if (isFilter)
                zProperty.poolSize = 0;

            zambeziConfig.properties.push_back(zProperty);
            zambeziConfig.property_status_map.insert(std::make_pair(propertyName, sProperty));
        }
        catch (XmlConfigParserException & e)
        {
            throw e;
        }
    }

    //// for virtual Property
    Iterator<Element> virtualproperty("VirtualProperty");
    for (virtualproperty = virtualproperty.begin(zambeziNode); virtualproperty != virtualproperty.end(); virtualproperty++)
    {
        try
        {
            // name
            ZambeziVirtualProperty vProperty;
            PropertyStatus  sProperty;
            sProperty.isCombined = true;
            getAttribute(virtualproperty.Get(), "name", vProperty.name);

            // weight
            //getAttribute_FloatType(indexing, "rankweight", rankWeight, false);
            float propertyWeight;
            if (getAttribute_FloatType(virtualproperty.Get(), "weight", propertyWeight, false))
            {
                vProperty.weight = propertyWeight;
                sProperty.weight = propertyWeight;
            }

            // poolsize
            uint32_t poolSize = 0;
            if (getAttribute_ByteSize(virtualproperty.Get(), "poolSize", poolSize, false))
                vProperty.poolSize = poolSize;
            else
                vProperty.poolSize = zambeziConfig.poolSize;

            // isAttrToken
            bool isAttrToken = false;
            if (getAttribute(virtualproperty.Get(), "isAttrToken", isAttrToken, false))
            {
                vProperty.isAttrToken = isAttrToken;
                if (isAttrToken)
                {
                    zambeziConfig.hasAttrtoken = true;
                    sProperty.isAttr = true;
                }
            }

            // SubProperty
            PropertyDataType dataType = UNKNOWN_DATA_PROPERTY_TYPE;
            Iterator<Element> subproperty("SubProperty");
            for (subproperty = subproperty.begin(virtualproperty.Get()); subproperty != subproperty.end(); subproperty++)
            {
                std::string subPropName;

                getAttribute(subproperty.Get(), "name", subPropName);

                PropertyConfigBase tmpConfig;
                tmpConfig.propertyName_ = subPropName;
                PropertyDataType dataType = UNKNOWN_DATA_PROPERTY_TYPE;
                dataType = collectionMeta.documentSchema_.find(tmpConfig)->propertyType_;

                if (dataType != STRING_PROPERTY_TYPE)
                {
                    throw XmlConfigParserException("VirtualProperty's subproperty only support STRING type!!!");
                }

                vProperty.subProperties.insert(subPropName);
            }
            vProperty.type = dataType;
            /// add default config
            sProperty.isFilter = false;
            sProperty.isTokenizer = true;

            zambeziConfig.virtualPropeties.push_back(vProperty);
            zambeziConfig.property_status_map.insert(std::make_pair(vProperty.name, sProperty));
        }
        catch (XmlConfigParserException & e)
        {
            throw e;
        }
    }

    std::string DictionaryPath;
    ticpp::Element* subNode = getUniqChildElement(zambeziNode, "TokenizeDictionary", false);
    if (subNode)
    {
        std::string DictionaryPath;
        getAttribute(subNode, "path", DictionaryPath);
        zambeziConfig.tokenPath = DictionaryPath;
    }
    else
    {
        if (!zambeziConfig.hasAttrtoken)
            throw XmlConfigParserException("[TokenizeDictionary] used in ZambeziConfig is missing.");
    }

    zambeziConfig.display();

    if (!zambeziConfig.checkConfig())
    {
        zambeziConfig.isEnable = false;
        LOG(ERROR) << "att_token index can not should not config togther with other index";
    }
    else
    {
        collectionMeta.indexBundleConfig_->isSchemaEnable_ = true;
    }
}

void CollectionConfig::parseProductRankingNode(
        const ticpp::Element* rankNode,
        CollectionMeta& collectionMeta) const
{
    if (!rankNode)
        return;

    MiningSchema& miningSchema =
        collectionMeta.miningBundleConfig_->mining_schema_;

    ProductRankingConfig& rankConfig = miningSchema.product_ranking_config;
    rankConfig.isEnable = true;
    getAttribute(rankNode, "debug", rankConfig.isDebug, false);

    Iterator<Element> it("Score");
    for (it = it.begin(rankNode); it != it.end(); ++it)
    {
        parseScoreNode(it.Get(), rankConfig);
    }

    std::string error;
    if (!rankConfig.checkConfig(collectionMeta, error))
    {
        throw XmlConfigParserException(error);
    }
}

void CollectionConfig::parseScoreNode(
        const ticpp::Element* scoreNode,
        ProductRankingConfig& rankConfig) const
{
    std::string typeName;
    getAttribute(scoreNode, "type", typeName);

    ProductScoreType scoreType = rankConfig.getScoreType(typeName);
    if (scoreType == PRODUCT_SCORE_NUM)
    {
        std::string error("unknown <Score> type \"" + typeName + "\"");
        throw XmlConfigParserException(error);
    }

    ProductScoreConfig& scoreConfig = rankConfig.scores[scoreType];
    parseScoreAttr(scoreNode, scoreConfig);

    Iterator<Element> it("Score");
    for (it = it.begin(scoreNode); it != it.end(); ++it)
    {
        ProductScoreConfig factorConfig;
        factorConfig.type = scoreType;

        parseScoreAttr(it.Get(), factorConfig);
        scoreConfig.factors.push_back(factorConfig);
    }
}

void CollectionConfig::parseScoreAttr(
        const ticpp::Element* scoreNode,
        ProductScoreConfig& scoreConfig) const
{
    getAttribute(scoreNode, "property", scoreConfig.propName, false);
    getAttribute_FloatType(scoreNode, "weight", scoreConfig.weight, false);
    getAttribute_FloatType(scoreNode, "min", scoreConfig.minLimit, false);
    getAttribute_FloatType(scoreNode, "max", scoreConfig.maxLimit, false);
    getAttribute(scoreNode, "debug", scoreConfig.isDebug, false);

    getAttribute_FloatType(scoreNode, "zoomin", scoreConfig.zoomin, false);

    if (scoreConfig.zoomin == 0)
    {
        throw XmlConfigParserException("Require non-zero value for <Score zoomin>");
    }

    getAttribute_FloatType(scoreNode, "logbase", scoreConfig.logBase, false);
    getAttribute_FloatType(scoreNode, "mean", scoreConfig.mean, false);
    getAttribute_FloatType(scoreNode, "deviation", scoreConfig.deviation, false);

    if (scoreConfig.logBase < 0 || scoreConfig.logBase == 1)
    {
        throw XmlConfigParserException("Require positive value for <Score logbase>");
    }
}

void CollectionConfig::parseIndexSchemaProperty(
        const ticpp::Element * property,
        CollectionMeta & collectionMeta)
{
    IndexBundleSchema& indexSchema = collectionMeta.indexBundleConfig_->indexSchema_;

    //holds all the configuration data of this property

    string propertyName;

    getAttribute(property, "name", propertyName);

    string pName = propertyName;
    boost::to_lower(pName);
    if ((pName == "date")||(pName == "docid"))
    {
        stringstream message;
        message << "DATE/DOCID are inherent properties and should not exist within IndexBundleSchema";
        throw XmlConfigParserException(message.str());
    }

    PropertyConfig p;
    p.setName(propertyName);

    IndexBundleSchema::iterator sp = indexSchema.find(p);
    PropertyConfig propertyConfig(*sp);
    indexSchema.erase(*sp);

    //set data --------------------------------------
    propertyConfig.setOriginalName(propertyName);
    propertyConfig.setIsIndex(true);

    //--- parse optional settings for "Property"

    //================== DISPLAY ==================

    Element * display = getUniqChildElement(property, "Display", false);

    // the setting is optional
    if (display)
    {
        PropertyDataType dataType;
        collectionMeta.getPropertyType(propertyName, dataType);
        if (dataType != STRING_PROPERTY_TYPE)
        {
            stringstream message;
            message << "\"" << propertyName << "\": Only a string type property can have <Display> option";
            throw XmlConfigParserException(message.str());
        }

        try
        {
            parseProperty_Display(display, propertyConfig);
        }
        catch (XmlConfigParserException & e)
        {
            stringstream ss;
            ss << " in \"" << propertyName << "\"";
            e.details_ += ss.str();
            throw e;
        }
    }

    //================== INDEXING ==================
    //TODO: if the property is set index="no", then no <Indexing> options???
    //there are several cases where it is allowed. Need to identify & narrow the cases
    Element * indexing = property->FirstChildElement("Indexing", false);

    //if <Indexing> element exists
    if (indexing)
    {
        //the number of original names. One alias name has to be the same as the original name
        int nOriginalName = 0;

        Iterator<Element> indexing_it("Indexing");

        // for all the <indexing> config
        for (indexing_it = indexing_it.begin(property); indexing_it != indexing_it.end(); indexing_it++)
        {
            try
            {
                parseProperty_Indexing(indexing_it.Get(), propertyConfig);
            }
            catch (ticpp::Exception & e)
            {
                stringstream ss;
                ss << " in \"" << propertyName << "\"";
                //e.m_details = e.m_details.substr(0, e.m_details.find_last_of("ticpp") - 5);
                e.m_details += ss.str();
                throw e;
            }

            if (propertyConfig.getName() == propertyConfig.getOriginalName())
            {
                nOriginalName++;
            }
            //add this property setting(alias) to DocumentSchema
            indexSchema.insert(propertyConfig);
        }

        // if (collectionMeta.isUnigramWildcard())
        {
            if ((SF1Config::get()->laConfigIdNameMap_.find("la_unigram")) == SF1Config::get()->laConfigIdNameMap_.end())
            {
                throw XmlConfigParserException("Undefined analyzer configuration id, \"la_unigram\"");
            }

            /// TODO, add a hidden alias here
            AnalysisInfo analysisInfo;
            analysisInfo.analyzerId_ = "la_unigram";
            SF1Config::get()->analysisPairList_.insert(analysisInfo);

            if (collectionMeta.indexBundleConfig_->bIndexUnigramProperty_)
            {
                propertyConfig.setName(propertyName+"_unigram");
                propertyConfig.setAnalysisInfo(analysisInfo);
                propertyConfig.setIsFilter(false);
                indexSchema.insert(propertyConfig);

                /// Attention
                /// update CollectionMeta
                /// xxx, Alais-ed property no need to be insert into collectionMeta
            }
        }

        if (nOriginalName == 0)
        {
            throw XmlConfigParserException("[Property : " + propertyConfig.getName()
                    + "] No <Indexing> setting for the original property name. (only alias?)");
        }
        else if (nOriginalName != 1)
        {
            throw XmlConfigParserException("[Property : " + propertyConfig.getName()
                    + "Only one alias name has to be the same as the property name");
        }
    }
    else
    {
        if (propertyConfig.isIndex())
        {
            throw XmlConfigParserException("[Property : " + propertyConfig.getName()
                    + " filter=\"yes/no\" or analyzer=\"\" required.");
        }
    }

    //add this property setting(alias) to DocumentSchema
    indexSchema.insert(propertyConfig);
}

void CollectionConfig::parseProperty_Display(const ticpp::Element * display, PropertyConfig & propertyConfig)
{
    int length = 0;
    bool bSnippet = false, bSummary = false;

    getAttribute(display, "length", length);
    getAttribute(display, "snippet", bSnippet, false);
    getAttribute(display, "summary", bSummary, false);

    if (length < 140 || length > 200) throw_TypeMismatch(display, "length", length, "140 to 200");

    propertyConfig.setDisplayLength(length);
    propertyConfig.setIsSnippet(bSnippet);
    propertyConfig.setIsSummary(bSummary);

    //==================== OPTIONAL SETTINGS ====================

    Element * settings = getUniqChildElement(display, "settings", false);
    if (settings)
    {
        unsigned int summaryNum = 0;

        if (getAttribute(settings, "summarynum", summaryNum, false))
        {
            if (summaryNum < 1 || summaryNum > 100) throw_TypeMismatch(settings, "summarynum", summaryNum, "1 to 100");
            propertyConfig.setSummaryNum(summaryNum);
        }
    }
} // END - CollectionConfig::parseProperty_Display()

// 3.2.2 Indexing -------------------------------------
void CollectionConfig::parseProperty_Indexing(const ticpp::Element * indexing, PropertyConfig & propertyConfig)
{
    string alias, analyzer, tokenizers;
    bool bFilter = false;
    bool bMultiValue = false;
    bool bRange = false;
    bool bStoreDocLen = true;
    float rankWeight = 0.0f;
    bool rtype = false;

    // read XML
    //
    getAttribute(indexing, "alias", alias, false);
    getAttribute(indexing, "analyzer", analyzer, false);
    getAttribute(indexing, "filter", bFilter, false);
    getAttribute(indexing, "multivalue", bMultiValue, false);
    getAttribute(indexing, "range", bRange, false);
    getAttribute(indexing, "doclen", bStoreDocLen, false);
    getAttribute(indexing, "tokenizer", tokenizers, false);
    getAttribute_FloatType(indexing, "rankweight", rankWeight, false);
    getAttribute(indexing, "rtype", rtype, false);

    downCase(analyzer);
    downCase(tokenizers);

    // handle exceptions
    //
    //if (!bFilter && analyzer.empty())
    //{
    //    stringstream msg;
    //    msg << propertyConfig.getName() << ": filter=\"yes/no\" or analyzer=\"\" required.";
    //    throw XmlConfigParserException(msg.str());
    //}

    if (propertyConfig.propertyType_ != STRING_PROPERTY_TYPE && !analyzer.empty())
    {
        stringstream msg;
        msg << propertyConfig.getName() << "\": \"analyzer\" can only be given to a \"string\" type property.";
        throw XmlConfigParserException(msg.str());
    }

    if (propertyConfig.propertyType_ != STRING_PROPERTY_TYPE && !tokenizers.empty())
    {
        stringstream msg;
        msg << propertyConfig.getName() << "\": \"analyzer\" can only be given to a \"string\" type property.";
        throw XmlConfigParserException(msg.str());
    }

    if (analyzer.empty() && !tokenizers.empty())
    {
        stringstream msg;
        msg << propertyConfig.getName() << "\": an \"analyzer\" is required as a pair of the tokenizer";
        throw XmlConfigParserException(msg.str());
    }

    if (!analyzer.empty() && rtype)
    {
        stringstream msg;
        msg << propertyConfig.getName() << "\": an \"rtype\" can not have any analyzer.";
        throw XmlConfigParserException(msg.str());
    }

    // save la-relate data
    //
    AnalysisInfo analysisInfo;

    if (!analyzer.empty())
    {
        analysisInfo.analyzerId_ = analyzer;

        if ((SF1Config::get()->laConfigIdNameMap_.find(analyzer)) == SF1Config::get()->laConfigIdNameMap_.end())
        {
            stringstream msg;
            msg << "Undefined analyzer configuration id, \"" << analyzer << "\"";
            throw XmlConfigParserException(msg.str());
        }

        vector<string> tokenizerIdList;
        parseByComma(tokenizers, tokenizerIdList);

        unsigned int i = 0;
        for (i = 0; i < tokenizerIdList.size(); i++)
        {
            if ((SF1Config::get()->tokenizerConfigNameMap_.find(tokenizerIdList[i])) == SF1Config::get()->tokenizerConfigNameMap_.end())
            {
                stringstream msg;
                msg << "Undefined tokenizer configuration id, \"" << tokenizerIdList[i] << "\"";
                throw XmlConfigParserException(msg.str());
            }
            analysisInfo.tokenizerNameList_.insert(tokenizerIdList[i]);
        }
    }

    if (!validateID(alias)) throw_TypeMismatch(indexing, "alias", alias, "Alphabets, Numbers, Dot(.), Dash(-) and Underscore(_)");
    // add alias name if any
    if (!alias.empty())
        propertyConfig.setName(alias);

    propertyConfig.setIsFilter(bFilter);
    propertyConfig.setIsMultiValue(bMultiValue);
    propertyConfig.setIsRange(bRange);
    propertyConfig.setIsStoreDocLen(bStoreDocLen);
    propertyConfig.setAnalysisInfo(analysisInfo);
    propertyConfig.setRankWeight(rankWeight);
    propertyConfig.setRType(rtype);

    // push to the list of all analysis Information in the configuration file.
    SF1Config::get()->analysisPairList_.insert(analysisInfo);

    // xxx, "la_sia_with_unigram" used for indexing, add "la_sia" for searching
    if (analyzer == "la_sia_with_unigram")
    {
        analysisInfo.analyzerId_ = "la_sia";
    }
    SF1Config::get()->analysisPairList_.insert(analysisInfo);
}

} // END - namespace sf1r
