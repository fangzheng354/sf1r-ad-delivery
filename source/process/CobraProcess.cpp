#include "CobraProcess.h"
#include <common/RouterInitializer.h>
#include <common/WorkerRouterInitializer.h>
#include <common/SFLogger.h>
#include <common/ResourceManager.h>

#include <log-manager/LogServerConnection.h>
#include <la-manager/LAPool.h>
#include <aggregator-manager/CollectionDataReceiver.h>
#include <node-manager/ZooKeeperManager.h>
#include <node-manager/SuperNodeManager.h>
#include <node-manager/NodeManagerBase.h>
#include <node-manager/DistributeSearchService.h>
#include <node-manager/DistributeRecommendService.h>
#include <node-manager/DistributeDriver.h>
#include <node-manager/DistributeRequestHooker.h>
#include <node-manager/RequestLog.h>
#include <node-manager/RecoveryChecker.h>
#include <node-manager/DistributeFileSyncMgr.h>
#include <node-manager/DistributeFileSys.h>

#include <common/OnSignal.h>
#include <common/XmlConfigParser.h>
#include <common/CollectionManager.h>
#include <common/CollectionTaskScheduler.h>

#include <util/ustring/UString.h>
#include <util/driver/IPRestrictor.h>
#include <util/driver/DriverConnectionFirewall.h>
#include <util/singleton.h>

#include <question-answering/QuestionAnalysis.h>
#include <ad-manager/AdFeedbackMgr.h>
#include <ad-manager/AdStreamSubscriber.h>

#include <laser-manager/LaserManager.h>

#include <boost/thread.hpp>
#include <boost/asio.hpp>
#include <boost/bind.hpp>
#include <boost/filesystem.hpp>
#include <boost/filesystem/path.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/algorithm/string.hpp>

#include <iostream>
#include <sstream>
#include <string>
#include <set>
#include <vector>
#include <map>

using namespace sf1r;
using namespace boost::filesystem;
using namespace izenelib::util;
using namespace izenelib::driver;

namespace bfs = boost::filesystem;

#define ipRestrictor ::izenelib::driver::IPRestrictor::getInstance()

bool CobraProcess::initialize(const std::string& configFileDir)
{
    if( !exists(configFileDir) || !is_directory(configFileDir) ) return false;
    try
    {
        configDir_ = configFileDir;
        boost::filesystem::path p(configFileDir);
        SF1Config::get()->setHomeDirectory(p.string());
        if( !SF1Config::get()->parseConfigFile( bfs::path(p/"sf1config.xml").string() ) )
        {
            return false;
        }
    }
    catch ( izenelib::util::ticpp::Exception & e )
    {
        cerr << e.what() << endl;
        return false;
    }

    if(!initKNlpWrapper()) return false;

    if(!initLogManager()) return false;

    if(!initFireWall()) return false;

    initDriverServer();

    initNodeManager();

    initAdServer();

    return true;
}

bool CobraProcess::initKNlpWrapper()
{
    const std::string dictDir = SF1Config::get()->getKNlpDictDir();
    boost::shared_ptr<KNlpWrapper> knlpWrapper(new KNlpWrapper(dictDir));
    KNlpResourceManager::setResource(knlpWrapper);
    return true;
}

bool CobraProcess::initLogManager()
{
    std::string log_conn = SF1Config::get()->getLogConnString();
    if (!sflog->init(log_conn))
    {
        std::cerr << "Init LogManager with " << log_conn << " failed!" << std::endl;
        return false;
    }
    const LogServerConnectionConfig& logServerConfig = SF1Config::get()->getLogServerConfig();
    if (!LogServerConnection::instance().init(logServerConfig))
    {
        std::cerr << "Init LogServerConnection with \"" << logServerConfig.host
                  << ":" << logServerConfig.rpcPort << "\" failed!" << std::endl;
        return false;
    }
    return true;
}

bool CobraProcess::initLAManager()
{
    // in collection config file, each <Indexing analyzer="..."> needs to be initialized in LAPool,
    // so LAPool is initialized here after all collection config files are parsed

    LAManagerConfig laConfig;
    SF1Config::get()->getLAManagerConfig(laConfig);

    ///TODO
    /// Ugly here, to be optimized through better configuration
    LAConfigUnit config2;
    config2.setId( "la_sia" );
    config2.setAnalysis( "korean" );
    config2.setMode( "label" );
    config2.setDictionaryPath( laConfig.kma_path_ ); // defined macro

    laConfig.addLAConfig(config2);

    AnalysisInfo analysisInfo2;
    analysisInfo2.analyzerId_ = "la_sia";
    analysisInfo2.tokenizerNameList_.insert("tok_divide");
    laConfig.addAnalysisPair(analysisInfo2);

    if (! LAPool::getInstance()->init(laConfig))
        return false;

    return true;
}

void CobraProcess::initQuery()
{
    ilplib::qa::QuestionAnalysis* pQA = Singleton<ilplib::qa::QuestionAnalysis>::get();
    const std::string& qahome = SF1Config::get()->getResourceDir();
    bfs::path path(bfs::path(qahome) / "qa" / "questionwords.txt");
    std::string qaPath = path.string();
    if( boost::filesystem::exists(qaPath) )
    {
        pQA->load(qaPath);
    }
}

bool CobraProcess::initFireWall()
{
    const FirewallConfig& fwConfig = SF1Config::get()->getFirewallConfig();
    std::vector<std::string>::const_iterator iter;
    for(iter = fwConfig.allowIPList_.begin(); iter != fwConfig.allowIPList_.end(); iter++)
        if ( !ipRestrictor->registerAllowIP( *iter ) )
            return false;
    for(iter = fwConfig.denyIPList_.begin(); iter != fwConfig.denyIPList_.end(); iter++)
        if ( !ipRestrictor->registerDenyIP( *iter ) )
            return false;
    return true;
}

bool CobraProcess::initDriverServer()
{
    const BrokerAgentConfig& baConfig = SF1Config::get()->getBrokerAgentConfig();
    std::size_t threadPoolSize = baConfig.threadNum_;
    bool enableTest = baConfig.enableTest_;
    unsigned int port = baConfig.port_;

    boost::asio::ip::tcp::endpoint endpoint(boost::asio::ip::tcp::v4(),port);

    //DriverThreadPool::init(1, 1);
    // init Router
    driverRouter_.reset(new ::izenelib::driver::Router);
    initQuery();
    initializeDriverRouter(*driverRouter_, NULL, enableTest);

    boost::shared_ptr<DriverConnectionFactory> factory(
        new DriverConnectionFactory(driverRouter_)
    );
    factory->setFirewall(DriverConnectionFirewall());

    driverServer_.reset(
        new DriverServer(endpoint, factory, threadPoolSize)
    );

    if (SF1Config::get()->isDistributedNode())
    {
        DistributeDriver::get()->init(driverRouter_);
    }

    addExitHook(boost::bind(&CobraProcess::stopDriver, this));

    return true;
}

bool CobraProcess::initNodeManager()
{
    SuperNodeManager::get()->init(SF1Config::get()->distributedCommonConfig_);

    // Do not connect to zookeeper if disabled
    if (SF1Config::get()->isDisableZooKeeper())
    {
        std::cout << "ZooKeeper is disabled!" << std::endl;
        return true;
    }

    // Start node management
    ZooKeeperManager::get()->init(
        SF1Config::get()->distributedUtilConfig_.zkConfig_,
        SF1Config::get()->distributedCommonConfig_.clusterId_);

    if (SF1Config::get()->isDistributedNode())
    {

        std::string dfs_local_root = SF1Config::get()->distributedUtilConfig_.dfsConfig_.mountDir_;
        if (!dfs_local_root.empty())
        {
            std::stringstream ss;
            ss << dfs_local_root << std::string("/sf1r/nodedata/")
                << SF1Config::get()->topologyConfig_.sf1rTopology_.clusterId_
                << std::string("/node") << getShardidStr(SF1Config::get()->topologyConfig_.sf1rTopology_.curNode_.nodeId_);
            dfs_local_root =  ss.str();
            LOG(INFO) << "local dfs enabled as : " << dfs_local_root;
            DistributeFileSys::get()->enableDFS(SF1Config::get()->distributedUtilConfig_.dfsConfig_.mountDir_,
                dfs_local_root);
        }

        NodeManagerBase::get()->init(SF1Config::get()->topologyConfig_);
        RecoveryChecker::get()->init(configDir_, SF1Config::get()->getWorkingDir(),
            SF1Config::get()->distributedCommonConfig_.check_level_);

        DistributeRequestHooker::get()->init();
        ReqLogMgr::initWriteRequestSet();
    }

    return true;
}

void CobraProcess::initAdServer()
{
    const AdCommonConfig& adconfig = SF1Config::get()->getAdCommonConfig();
    if (adconfig.is_enabled)
    {
        LOG(INFO) << "ad server enabled";
        AdFeedbackMgr::get()->init(adconfig.dmp_ip, adconfig.dmp_port);
        AdStreamSubscriber::get()->init(adconfig.stream_log_ip, adconfig.stream_log_port);
    }
}

void CobraProcess::stopAdServer()
{
    const AdCommonConfig& adconfig = SF1Config::get()->getAdCommonConfig();
    if (adconfig.is_enabled)
    {
        AdStreamSubscriber::get()->stop();
        AdFeedbackMgr::get()->stop();
    }
}

void CobraProcess::stopDriver()
{
    if (SF1Config::get()->isDistributedNode())
    {
        DistributeDriver::get()->stop();
    }
    if (driverServer_)
    {
        driverServer_->stop();
    }
    stopAdServer();
}

bool CobraProcess::startDistributedServer()
{
    if (SF1Config::get()->isDisableZooKeeper())
    {
        std::cout << "ZooKeeper is disabled!" << std::endl;
        return true;
    }

    // Start data receiver
    unsigned int dataPort = SF1Config::get()->distributedCommonConfig_.dataRecvPort_;
    CollectionDataReceiver::get()->init(dataPort, "./collection"); //xxx
    CollectionDataReceiver::get()->start();

    DistributeFileSyncMgr::get()->init();

    // Start worker server
    if (SF1Config::get()->isWorkerEnabled())
    {
        workerRouter_.reset(new net::aggregator::WorkerRouter);
        WorkerRouterInitializer routerInitializer;
        if (!routerInitializer.initRouter(*workerRouter_))
            return false;

        std::string localHost = SF1Config::get()->distributedCommonConfig_.localHost_;
        uint16_t workerPort = SF1Config::get()->distributedCommonConfig_.workerPort_;
        std::size_t threadNum = SF1Config::get()->brokerAgentConfig_.threadNum_;

        cout << "[WorkerServer] listen at "<<localHost<<":"<<workerPort<<endl;

        workerServer_.reset(new net::aggregator::WorkerServer(
            *workerRouter_, localHost, workerPort, threadNum*2));
        workerServer_->start();
    }

    // Start server for master
    if (SF1Config::get()->isMasterEnabled())
    {
        std::string localHost = SF1Config::get()->distributedCommonConfig_.localHost_;
        uint16_t masterPort = SF1Config::get()->distributedCommonConfig_.masterPort_;
        masterServer_.reset(new MasterServer);
        masterServer_->start(localHost, masterPort);
    }

    // Start distributed topology node manager(s)
    if (SF1Config::get()->isDistributedNode())
    {
        // register distribute services.
        NodeManagerBase::get()->registerDistributeService(
            boost::shared_ptr<IDistributeService>(new DistributeSearchService()),
            SF1Config::get()->isSearchWorker(),
            SF1Config::get()->isSearchMaster());
        NodeManagerBase::get()->registerDistributeService(
            boost::shared_ptr<IDistributeService>(new DistributeRecommendService()),
            SF1Config::get()->isRecommendWorker(),
            SF1Config::get()->isRecommendMaster());

        NodeManagerBase::get()->start();
    }

    addExitHook(boost::bind(&CobraProcess::stopDistributedServer, this));
    return true;
}

void CobraProcess::stopDistributedServer()
{
    ZooKeeperManager::get()->stop();

    if (workerServer_)
        workerServer_->stop();

    CollectionDataReceiver::get()->stop();
    DistributeFileSyncMgr::get()->stop();
    RecoveryChecker::clearForceExitFlag();
}

//void CobraProcess::scheduleTask(const std::string& collection)
//{
//    CollectionManager::MutexType* mutex = CollectionManager::get()->getCollectionMutex(collection);
//    CollectionManager::ScopedReadLock rlock(*mutex);
//
//    CollectionHandler* collectionHandler = CollectionManager::get()->findHandler(collection);
//    CollectionTaskScheduler::get()->schedule(collectionHandler);
//#ifdef COBRA_RESTRICT
//    	CollectionTaskScheduler::get()->scheduleLicenseTask(collection);
//#endif // COBRA_RESTRICT
//}

void CobraProcess::startCollections()
{
    bfs::directory_iterator iter(configDir_), end_iter;
    for(; iter!= end_iter; ++iter)
    {
        if(bfs::is_regular_file(*iter))
        {
            if(bfs::path(*iter).filename().string().rfind(".xml") == (bfs::path(*iter).filename().string().length() - std::string(".xml").length()))
                if(!boost::iequals(bfs::path(*iter).filename().string(),"sf1config.xml"))
                {
                    std::string collectionName = bfs::path(*iter).filename().string().substr(0,bfs::path(*iter).filename().string().rfind(".xml"));
                    CollectionManager::get()->startCollection(collectionName, bfs::path(*iter).string(),
                        false, true);
                    //scheduleTask(collectionName);
                }
        }
    }

    if (SF1Config::get()->isDistributedNode())
    {
        NodeManagerBase::get()->updateTopologyCfg(SF1Config::get()->topologyConfig_.sf1rTopology_);
    }
}

void CobraProcess::stopCollections()
{
    SF1Config::CollectionMetaMap collectionMetaMap = SF1Config::get()->mutableCollectionMetaMap();
    SF1Config::CollectionMetaMap::iterator collectionIter = collectionMetaMap.begin();
    for(; collectionIter != collectionMetaMap.end(); collectionIter++)
    {
        CollectionMeta& collectionMeta = collectionIter->second;
        std::string collectionName = collectionMeta.getName();
        CollectionManager::get()->stopCollection(collectionName);
    }
    CollectionManager::get()->getOSGILauncher().stop();
}

int CobraProcess::run()
{

    bool caughtException = false;

    try
    {
        LAPool::getInstance()->initLangAnalyzer();

        startCollections();

        if(!initLAManager())
            throw std::runtime_error("failed in initLAManager()");

        if (!startDistributedServer())
            throw std::runtime_error("failed in startDistributedServer()");

        LOG(INFO) << "CobraProcess has started";

        driverServer_->run();

        stopCollections();
        closeLaserDependency();
        LOG(INFO) << "CobraProcess has exited";
        waitSignalThread();
    }
    catch (const std::exception& e)
    {
        caughtException = true;
        LOG(ERROR) << "CobraProcess has aborted by std exception: " << e.what();
    }
    catch (...)
    {
        caughtException = true;
        LOG(ERROR) << "CobraProcess has aborted by unknown exception";
    }

    return caughtException ? 1 : 0;
}
