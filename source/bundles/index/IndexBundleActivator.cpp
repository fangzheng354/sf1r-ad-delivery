#include "IndexBundleActivator.h"
#include <bundles/mining/MiningSearchService.h>

#include <common/SFLogger.h>
#include <common/Utilities.h>
#include <index-manager/InvertedIndexManager.h>
#include <index-manager/ZambeziIndexManager.h>
#include <index-manager/zambezi-manager/ZambeziManager.h>
#include <search-manager/SearchFactory.h>
#include <search-manager/SearchManager.h>
#include <search-manager/QueryPruneFactory.h>
#include <ranking-manager/RankingManager.h>
#include <document-manager/DocumentManager.h>
#include <la-manager/LAManager.h>
#include <la-manager/LAPool.h>
#include <la-manager/AttrTokenizeWrapper.h>
#include <aggregator-manager/SearchMerger.h>
#include <aggregator-manager/SearchWorker.h>
#include <aggregator-manager/IndexWorker.h>
#include <node-manager/MasterManagerBase.h>
#include <node-manager/Sf1rTopology.h>
#include <node-manager/RecoveryChecker.h>
#include <util/singleton.h>
#include <ad-manager/AdSearchService.h>

#include <boost/filesystem.hpp>

#include <memory> // for auto_ptr

namespace bfs = boost::filesystem;
using namespace izenelib::util;

namespace sf1r
{

using namespace izenelib::osgi;
IndexBundleActivator::IndexBundleActivator()
    : miningSearchTracker_(0)
    , miningTaskTracker_(0)
    , context_(0)
    , searchService_(0)
    , searchServiceReg_(0)
    , taskService_(0)
    , taskServiceReg_(0)
    , config_(0)
    , zambeziManager_(NULL)
{
}

IndexBundleActivator::~IndexBundleActivator()
{
}

void IndexBundleActivator::start( IBundleContext::ConstPtr context )
{
    context_ = context;

    boost::shared_ptr<BundleConfiguration> bundleConfigPtr = context->getBundleConfig();
    config_ = static_cast<IndexBundleConfiguration*>(bundleConfigPtr.get());
    init_();

    Properties props;
    props.put( "collection", config_->collectionName_);
    searchServiceReg_ = context->registerService( "IndexSearchService", searchService_, props );
    taskServiceReg_ = context->registerService( "IndexTaskService", taskService_, props );
    miningSearchTracker_ = new ServiceTracker( context, "MiningSearchService", this );
    miningSearchTracker_->startTracking();
    miningTaskTracker_ = new ServiceTracker( context, "MiningTaskService", this );
    miningTaskTracker_->startTracking();
}

void IndexBundleActivator::stop( IBundleContext::ConstPtr context )
{
    if (config_->isNormalSchemaEnable_)
    {
        invertedIndexManager_->flush(false);
    }

    if (config_->isZambeziSchemaEnable_)
    {
        zambeziIndexManager_->postProcessForAPI();    
    }

    if(miningSearchTracker_)
    {
        miningSearchTracker_->stopTracking();
        delete miningSearchTracker_;
        miningSearchTracker_ = 0;
    }
    if(miningTaskTracker_)
    {
        miningTaskTracker_->stopTracking();
        delete miningTaskTracker_;
        miningTaskTracker_ = 0;
    }

    if(searchServiceReg_)
    {
        searchServiceReg_->unregister();
        delete searchServiceReg_;
        delete searchService_;
        searchServiceReg_ = 0;
        searchService_ = 0;
    }
    if(taskServiceReg_)
    {
        taskServiceReg_->unregister();
        delete taskServiceReg_;
        delete taskService_;
        taskServiceReg_ = 0;
        taskService_ = 0;
    }

    MasterManagerBase::get()->unregisterAggregator(searchAggregator_);
    MasterManagerBase::get()->unregisterAggregator(ro_searchAggregator_, true);
    MasterManagerBase::get()->unregisterAggregator(indexAggregator_);

    if (zambeziManager_)
    {
        delete zambeziManager_;
    }
    // TODO flush and delete
}

bool IndexBundleActivator::addingService( const ServiceReference& ref )
{
    if ( ref.getServiceName() == "MiningSearchService" )
    {
        Properties props = ref.getServiceProperties();
        if ( props.get( "collection" ) == config_->collectionName_)
        {
            MiningSearchService* service = reinterpret_cast<MiningSearchService*> ( const_cast<IService*>(ref.getService()) );
            cout << "[IndexBundleActivator#addingService] Calling MiningSearchService..." << endl;
            searchService_->searchWorker_->miningManager_ = service->GetMiningManager();
            searchService_->searchMerger_->miningManager_ = service->GetMiningManager();

            searchService_->searchWorker_->queryPruneFactory_->init(searchService_->searchWorker_->miningManager_);

            searchManager_->setMiningManager(service->GetMiningManager());
            return true;
        }
        else
        {
            return false;
        }
    }
    else if ( ref.getServiceName() == "MiningTaskService" )
    {
        Properties props = ref.getServiceProperties();
        if ( props.get( "collection" ) == config_->collectionName_)
        {
            MiningTaskService* service = reinterpret_cast<MiningTaskService*> ( const_cast<IService*>(ref.getService()) );
            cout << "[IndexBundleActivator#addingService] Calling MiningTaskService..." << endl;
            taskService_->indexWorker_->miningTaskService_= service;
            return true;
        }
        else
        {
            return false;
        }
    }
    else if ( ref.getServiceName() == "ProductSearchService" )
    {
        Properties props = ref.getServiceProperties();
        if ( props.get( "collection" ) == config_->collectionName_)
        {
//             ProductSearchService* service = reinterpret_cast<ProductSearchService*> ( const_cast<IService*>(ref.getService()) );
            cout << "[IndexBundleActivator#addingService] Calling ProductSearchService..." << endl;
            // product index hook set in Product Bundle

            return true;
        }
        else
        {
            return false;
        }
    }
    else if ( ref.getServiceName() == "ProductTaskService" )
    {
        Properties props = ref.getServiceProperties();
        if ( props.get( "collection" ) == config_->collectionName_)
        {
            //ProductTaskService* service = reinterpret_cast<ProductTaskService*> ( const_cast<IService*>(ref.getService()) );
            cout << "[IndexBundleActivator#addingService] Calling ProductTaskService..." << endl;
            ///TODO
            return true;
        }
        else
        {
            return false;
        }
    }
    else
    {
        return false;
    }
}

void IndexBundleActivator::removedService( const ServiceReference& ref )
{

}

bool IndexBundleActivator::init_()
{
    std::cout<<"["<<config_->collectionName_<<"]"<<"[IndexBundleActivator] open data directories.."<<std::endl;
    bool bOpenDataDir = openDataDirectories_();
    SF1R_ENSURE_INIT(bOpenDataDir);
    LOG(INFO)<<"["<<config_->collectionName_<<"]"<<"[IndexBundleActivator] working directory "<<currentCollectionDataName_<<std::endl;
    std::cout<<"["<<config_->collectionName_<<"]"<<"[IndexBundleActivator] open id manager.."<<std::endl;
    
    idManager_ = createIDManager_();
    SF1R_ENSURE_INIT(idManager_);
    
    laManager_ = createLAManager_();
    SF1R_ENSURE_INIT(laManager_);
    
    SF1R_ENSURE_INIT(initializeQueryManager_());
    
    std::cout<<"["<<config_->collectionName_<<"]"<<"[IndexBundleActivator] open document manager.."<<std::endl;
    documentManager_ = createDocumentManager_();
    SF1R_ENSURE_INIT(documentManager_);
    documentManager_->setZambeziConfig(config_->zambeziConfig_);

    /*
    Here, the NormalSchemaEnable must be true now, because the Schema must be used as a filter in documentSearch;
    Zambezi search now not support condition(filter) 2013.10.24;
    */
    if(config_->isNormalSchemaEnable_)
    {    
        std::cout<<"["<<config_->collectionName_<<"]"<<"[IndexBundleActivator] open normal index manager.."<<std::endl;
        invertedIndexManager_ = createInvertedIndexManager_();
        SF1R_ENSURE_INIT(invertedIndexManager_);
    }
    
    std::cout<<"["<<config_->collectionName_<<"]"<<"[IndexBundleActivator] open ranking manager.."<<std::endl;
    rankingManager_ = createRankingManager_();
    SF1R_ENSURE_INIT(rankingManager_);

    if (config_->isZambeziSchemaEnable_)
    {
        if (!config_->zambeziConfig_.isEnable)
            return false;
        std::cout<<"["<<config_->collectionName_<<"]"<<"[IndexBundleActivator] open zambezi index manager.."<<std::endl;

        if (!createZambeziManager_())
            return false;
        zambeziIndexManager_ = createZambeziIndexManager_();
        SF1R_ENSURE_INIT(zambeziIndexManager_);
    }

    std::cout<<"["<<config_->collectionName_<<"]"<<"[IndexBundleActivator] open search manager.."<<std::endl;
    searchManager_ = createSearchManager_();
    SF1R_ENSURE_INIT(searchManager_);
    
    searchWorker_ = createSearchWorker_();
    SF1R_ENSURE_INIT(searchWorker_);
    
    adSearchService_ = createAdSearchService(searchWorker_.get());

    searchAggregator_ = createSearchAggregator_(false);
    SF1R_ENSURE_INIT(searchAggregator_);
    
    if (MasterManagerBase::get()->isMasterEnabled())
    {
        ro_searchAggregator_ = createSearchAggregator_(true);
        SF1R_ENSURE_INIT(ro_searchAggregator_);
    }

    indexWorker_ = createIndexWorker_();
    SF1R_ENSURE_INIT(indexWorker_);

    std::cout<<"["<<config_->collectionName_<<"]"<<"[IndexBundleActivator] open index worker.."<<std::endl;
    // add all kinds of index that will support increment build.
    if (config_->isNormalSchemaEnable_)
        indexWorker_->getIncSupportedIndexManager().addIndex(invertedIndexManager_);

    indexWorker_->getIncSupportedIndexManager().setDocumentManager(documentManager_);
    
    if (config_->isZambeziSchemaEnable_)
        indexWorker_->getIncSupportedIndexManager().addIndex(zambeziIndexManager_);

    indexAggregator_ = createIndexAggregator_();
    SF1R_ENSURE_INIT(indexAggregator_);

    searchService_ = new IndexSearchService(config_);

    searchService_->searchAggregator_ = searchAggregator_;
    //if (MasterManagerBase::get()->isOnlyMaster())
    {
        searchService_->ro_searchAggregator_ = ro_searchAggregator_;
    }
    //else
    //{
    //    searchService_->ro_searchAggregator_ = searchAggregator_;
    //}
    searchService_->searchMerger_ = searchMerger_.get();
    searchService_->searchWorker_ = searchWorker_;
    searchService_->searchWorker_->laManager_ = laManager_;
    searchService_->searchWorker_->idManager_ = idManager_;
    searchService_->searchWorker_->documentManager_ = documentManager_;
    searchService_->searchWorker_->invertedIndexManager_ = invertedIndexManager_;
    //searchService_->searchWorker_->rankingManager_ = rankingManager_;
    searchService_->searchWorker_->searchManager_ = searchManager_;
    searchService_->adSearchService_ = adSearchService_;

    taskService_ = new IndexTaskService(config_);
    indexWorker_->sharding_strategy_ = taskService_->sharding_strategy_;
    searchMerger_->sharding_strategy_ = taskService_->sharding_strategy_;

    taskService_->indexAggregator_ = indexAggregator_;
    taskService_->indexWorker_ = indexWorker_;
    taskService_->indexWorker_->idManager_ = idManager_;
    //taskService_->indexWorker_->laManager_ = laManager_;
    taskService_->indexWorker_->documentManager_ = documentManager_;
    taskService_->indexWorker_->searchWorker_= searchWorker_;
    taskService_->indexWorker_->summarizer_.init(LAPool::getInstance()->getLangId(), idManager_);

    return true;
}

std::string IndexBundleActivator::getCurrentCollectionDataPath_() const
{
    return config_->collPath_.getCollectionDataPath()+"/"+currentCollectionDataName_;
}

std::string IndexBundleActivator::getCollectionDataPath_() const
{
    return config_->collPath_.getCollectionDataPath();
}

std::string IndexBundleActivator::getQueryDataPath_() const
{
    return config_->collPath_.getQueryDataPath();
}

bool IndexBundleActivator::openDataDirectories_()
{
    bfs::create_directories(config_->indexSCDPath());
    bfs::create_directories(config_->masterIndexSCDPath());
    bfs::create_directories(config_->rebuildIndexSCDPath());
    bfs::create_directories(config_->logSCDPath());

    std::vector<std::string>& directories = config_->collectionDataDirectories_;
    if( directories.size() == 0 )
    {
        LOG(ERROR)<<"no data dir config"<<std::endl;
        return false;
    }
    directoryRotator_.setCapacity(directories.size());
    std::vector<bfs::path> dirtyDirectories;
    typedef std::vector<std::string>::const_iterator iterator;
    for (iterator it = directories.begin(); it != directories.end(); ++it)
    {
        bfs::path dataDir = bfs::path( getCollectionDataPath_() ) / *it;
        if (!directoryRotator_.appendDirectory(dataDir))
        {
            std::string msg = dataDir.string() + " corrupted, delete it!";
            LOG(ERROR) <<msg <<endl;
            //clean the corrupt dir
            boost::filesystem::remove_all( dataDir );
            dirtyDirectories.push_back(dataDir);
            if (MasterManagerBase::get()->isDistributed())
            {
                RecoveryChecker::get()->setRollbackFlag(0);
                RecoveryChecker::forceExit("exit for corrupted collection data. please restart to start auto rollback.");
            }
        }
    }

    directoryRotator_.rotateToNewest();
    boost::shared_ptr<Directory> newest = directoryRotator_.currentDirectory();
    if (newest)
    {
        bfs::path p = newest->path();
        currentCollectionDataName_ = p.filename().string();
        config_->collPath_.setCurrCollectionDir(currentCollectionDataName_);
        std::vector<bfs::path>::iterator it = dirtyDirectories.begin();
        for( ; it != dirtyDirectories.end(); ++it)
            directoryRotator_.appendDirectory(*it);
        return true;
    }
    else
    {
        std::vector<bfs::path>::iterator it = dirtyDirectories.begin();
        for( ; it != dirtyDirectories.end(); ++it)
            directoryRotator_.appendDirectory(*it);

        directoryRotator_.rotateToNewest();
        boost::shared_ptr<Directory> dir = directoryRotator_.currentDirectory();
        if(dir)
        {
            currentCollectionDataName_ = dir->path().filename().string();
            config_->collPath_.setCurrCollectionDir(currentCollectionDataName_);
            return true;
        }
    }

    return false;
}

boost::shared_ptr<IDManager>
IndexBundleActivator::createIDManager_() const
{
    std::string dir = getCurrentCollectionDataPath_()+"/id/";
    boost::filesystem::create_directories(dir);

    boost::shared_ptr<IDManager> ret(
        new IDManager(dir)
    );

    return ret;
}

boost::shared_ptr<DocumentManager>
IndexBundleActivator::createDocumentManager_() const
{
    std::string dir = getCurrentCollectionDataPath_()+"/dm/";
    boost::filesystem::create_directories(dir);
    boost::shared_ptr<DocumentManager> ret(
        new DocumentManager(
            dir,
            config_->indexSchema_,
            config_->encoding_,
            config_->documentCacheNum_
        )
    );

    return ret;
}

boost::shared_ptr<InvertedIndexManager>
IndexBundleActivator::createInvertedIndexManager_() const
{
    std::string dir = getCurrentCollectionDataPath_()+"/index/";
    boost::filesystem::create_directories(dir);
    boost::shared_ptr<InvertedIndexManager> ret;

    ret.reset(new InvertedIndexManager(config_));
    if (ret)
    {
        IndexManagerConfig config(config_->indexConfig_);
        config.indexStrategy_.indexLocation_ = dir;


        IndexerCollectionMeta indexCollectionMeta;
        indexCollectionMeta.setName(config_->collectionName_);

        const IndexBundleSchema& indexSchema = config_->indexSchema_;
        for (IndexBundleSchema::const_iterator iter = indexSchema.begin(), iterEnd = indexSchema.end();
            iter != iterEnd; ++iter)
        {
            IndexerPropertyConfig indexerPropertyConfig(
                iter->getPropertyId(),
                iter->getName(),
                iter->isIndex(),
                iter->isAnalyzed()
            );
            indexerPropertyConfig.setIsFilter(iter->getIsFilter());
            indexerPropertyConfig.setIsMultiValue(iter->getIsMultiValue());
            indexerPropertyConfig.setIsStoreDocLen(iter->getIsStoreDocLen());
            PropertyDataType sf1r_type = iter->getType();
//             LOG(INFO)<<"Find property "<<iter->getName()<<","<<sf1r_type<<std::endl;
            izenelib::ir::indexmanager::PropertyType type;
            if(Utilities::convertPropertyDataType(iter->getName(), sf1r_type, type))
            {
//                 LOG(INFO)<<"Index get property "<<iter->getName()<<","<<type.which()<<std::endl;
                indexerPropertyConfig.setType(type);
            }
            indexCollectionMeta.addPropertyConfig(indexerPropertyConfig);
        }

        config.addCollectionMeta(indexCollectionMeta);

        std::map<std::string, unsigned int> collectionIdMapping;
        collectionIdMapping[config_->collectionName_] = 1;

        ret->setIndexManagerConfig(config, collectionIdMapping);
        ret->idManager_ = idManager_;
        ret->laManager_ = laManager_;
        ret->documentManager_ = documentManager_;
    }
    return ret;
}

boost::shared_ptr<RankingManager>
IndexBundleActivator::createRankingManager_() const
{
    boost::shared_ptr<RankingManager> ret(new RankingManager);
    ret->init(config_->rankingManagerConfig_.rankingConfigUnit_);

    typedef std::map<std::string, float>::const_iterator weight_map_iterator;
    PropertyConfig propertyConfigOut;
    for (weight_map_iterator it = config_->rankingManagerConfig_.propertyWeightMapByProperty_.begin(),
                                        itEnd = config_->rankingManagerConfig_.propertyWeightMapByProperty_.end();
          it != itEnd; ++it)
    {
        if (config_->getPropertyConfig(it->first, propertyConfigOut))
        {
            ret->setPropertyWeight(propertyConfigOut.getPropertyId(), it->second);
        }
    }
    return ret;
}

bool IndexBundleActivator::createZambeziManager_()
{
    if (config_->zambeziConfig_.hasAttrtoken && 
        !AttrTokenizeWrapper::get()->loadDictFiles(config_->zambeziConfig_.system_resource_path_ + "/dict/" + config_->zambeziConfig_.tokenPath))
        return false;

    std::string dir = getCurrentCollectionDataPath_()+"/zambezi/";
    const bfs::path zambeziDir(dir);

    config_->zambeziConfig_.indexFilePath = dir + "index_bin";
    bfs::create_directories(zambeziDir);

    if (zambeziManager_) delete zambeziManager_;
    zambeziManager_ = new ZambeziManager(config_->zambeziConfig_);

    if (!zambeziManager_->open())
        return false;

    return true;
}

boost::shared_ptr<IIncSupportedIndex>
IndexBundleActivator::createZambeziIndexManager_() const
{
    boost::shared_ptr<IIncSupportedIndex> ret(new ZambeziIndexManager
                                             (config_->zambeziConfig_,
                                              zambeziManager_->getProperties(),
                                              zambeziManager_->getIndexMap(),
                                              zambeziManager_->getTokenizer(),
                                              documentManager_));
    return ret;
}

boost::shared_ptr<SearchManager>
IndexBundleActivator::createSearchManager_() const
{
    boost::shared_ptr<SearchManager> ret;

    if (documentManager_ && rankingManager_ && (invertedIndexManager_ || zambeziManager_))
    {

        SearchFactory factory(*config_,
                          documentManager_,
                          invertedIndexManager_,
                          rankingManager_,
                          zambeziManager_);
        ret.reset(new SearchManager(*config_, factory));
    }
    return ret;
}

boost::shared_ptr<LAManager>
IndexBundleActivator::createLAManager_() const
{
    boost::shared_ptr<LAManager> ret(new LAManager());
    std::string kma_path;
    LAPool::getInstance()->get_kma_path(kma_path);
    string temp = kma_path + "/stopword.txt";
    ret->loadStopDict( temp );
    return ret;
}

boost::shared_ptr<SearchWorker>
IndexBundleActivator::createSearchWorker_()
{
    boost::shared_ptr<SearchWorker> ret(new SearchWorker(config_));
    return ret;
}

boost::shared_ptr<AdSearchService>
IndexBundleActivator::createAdSearchService(SearchWorker* searchWorker)
{
    boost::shared_ptr<AdSearchService> ret(new AdSearchService(searchWorker));
    ret->init("", "beta", config_->collectionName_);
    return ret;
}

boost::shared_ptr<SearchAggregator>
IndexBundleActivator::createSearchAggregator_(bool readonly)
{
    if (!searchMerger_)
        searchMerger_.reset(new SearchMerger());

    std::auto_ptr<SearchMergerProxy> mergerProxy(new SearchMergerProxy(searchMerger_.get()));
    searchMerger_->bindCallProxy(*mergerProxy);

    std::auto_ptr<SearchWorkerProxy> localWorkerProxy(new SearchWorkerProxy(searchWorker_.get()));
    searchWorker_->bindCallProxy(*localWorkerProxy);

    boost::shared_ptr<SearchAggregator> ret(
        new SearchAggregator(mergerProxy.get(), localWorkerProxy.get(),
            Sf1rTopology::getServiceName(Sf1rTopology::SearchService), config_->collectionName_));

    mergerProxy.release();
    localWorkerProxy.release();

    // workers will be detected and set by master node manager
    MasterManagerBase::get()->registerAggregator(ret, readonly);
    return ret;
}

boost::shared_ptr<IndexWorker>
IndexBundleActivator::createIndexWorker_()
{
    boost::shared_ptr<IndexWorker> ret(new IndexWorker(config_, directoryRotator_));
    return ret;
}

boost::shared_ptr<IndexAggregator>
IndexBundleActivator::createIndexAggregator_()
{
    indexMerger_.reset(new IndexMerger);

    std::auto_ptr<IndexMergerProxy> mergerProxy(new IndexMergerProxy(indexMerger_.get()));
    indexMerger_->bindCallProxy(*mergerProxy);

    std::auto_ptr<IndexWorkerProxy> localWorkerProxy(new IndexWorkerProxy(indexWorker_.get()));
    indexWorker_->bindCallProxy(*localWorkerProxy);

    boost::shared_ptr<IndexAggregator> ret(
        new IndexAggregator(mergerProxy.get(), localWorkerProxy.get(),
            Sf1rTopology::getServiceName(Sf1rTopology::SearchService), config_->collectionName_));

    mergerProxy.release();
    localWorkerProxy.release();

    MasterManagerBase::get()->registerAggregator(ret);
    return ret;
}

bool IndexBundleActivator::initializeQueryManager_() const
{
    // initialize Query Parser
    QueryParser::initOnlyOnce();

    std::string kma_path;
    LAPool::getInstance()->get_kma_path(kma_path);
    std::string restrictDictPath = kma_path + "/restrict.txt";
    QueryUtility::buildRestrictTermDictionary( restrictDictPath, idManager_);

    return true;
}


}
