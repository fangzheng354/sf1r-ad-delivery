#include "IndexSearchService.h"

#include <node-manager/MasterManagerBase.h>
#include <aggregator-manager/SearchMerger.h>
#include <aggregator-manager/SearchWorker.h>

#include <common/SearchCache.h>
#include <common/SFLogger.h>
#include <common/type_defs.h>

namespace sf1r
{

const static int CACHE_THRESHOLD = 100;

IndexSearchService::IndexSearchService(IndexBundleConfiguration* config)
    : bundleConfig_(config)
    , searchMerger_(NULL)
    , searchCache_(new SearchCache(bundleConfig_->masterSearchCacheNum_,
                                    bundleConfig_->refreshCacheInterval_,
                                    bundleConfig_->refreshSearchCache_))
{
    ro_index_ = 0;
}

IndexSearchService::~IndexSearchService()
{
}

boost::shared_ptr<SearchAggregator> IndexSearchService::getSearchAggregator()
{
    return searchAggregator_;
}

const IndexBundleConfiguration* IndexSearchService::getBundleConfig()
{
    return bundleConfig_;
}

void IndexSearchService::OnUpdateSearchCache()
{
    LOG(INFO) << "clearing master search cache.";
    searchCache_->clear();
}

bool IndexSearchService::getSearchResult(
    KeywordSearchActionItem& actionItem,
    KeywordSearchResult& resultItem
)
{
    CREATE_SCOPED_PROFILER (query, "IndexSearchService", "processGetSearchResults all: total query time");

    LOG(INFO) << "Search Begin." << endl;
    if (!bundleConfig_->isMasterAggregator() || !searchAggregator_->isNeedDistribute())
    {
        bool ret = searchWorker_->doLocalSearch(actionItem, resultItem);
        net::aggregator::WorkerResults<KeywordSearchResult> workerResults;
        workerResults.add(0, resultItem);
        LOG(INFO) << "Local Search End." << endl;
        return ret;
    }


    /// Perform distributed search by aggregator
    KeywordSearchResult distResultItem;
    distResultItem.distSearchInfo_.isDistributed_ = true;
    distResultItem.distSearchInfo_.effective_ = true;
    distResultItem.distSearchInfo_.nodeType_ = DistKeywordSearchInfo::NODE_WORKER;

    uint32_t request_index = ++ro_index_;

    if (actionItem.searchingMode_.mode_ == SearchingMode::WAND)
    {
        distResultItem.distSearchInfo_.option_ = DistKeywordSearchInfo::OPTION_GATHER_INFO;
        bool ret = ro_searchAggregator_->distributeRequest<KeywordSearchActionItem, DistKeywordSearchInfo>(
            actionItem.collectionName_, request_index, "getDistSearchInfo", actionItem, distResultItem.distSearchInfo_);

        if (!ret)
        {
            LOG(ERROR) << "get dist search info error.";
            return false;
        }

        distResultItem.distSearchInfo_.option_ = DistKeywordSearchInfo::OPTION_CARRIED_INFO;
    }

    typedef std::map<workerid_t, KeywordSearchResult> ResultMapT;
    typedef ResultMapT::iterator ResultMapIterT;

    QueryIdentity identity;
    // For distributed search, as it should merge the results over all nodes,
    // the topK start offset is fixed to zero
    size_t topKStart = actionItem.pageInfo_.topKStart(bundleConfig_->topKNum_, IsTopKComesFromConfig(actionItem));
    LOG(INFO) << "query: " << actionItem.env_.queryString_ << ", topKStart for dist search is " << topKStart << ", pageInfo_ :"
        << actionItem.pageInfo_.start_ << ", " << actionItem.pageInfo_.count_;
    searchWorker_->makeQueryIdentity(identity, actionItem, distResultItem.distSearchInfo_.option_, topKStart);

    bool ret = true;
    struct timespec start_time;
    clock_gettime(CLOCK_MONOTONIC, &start_time);
    //gettimeofday(&start_time, 0);
    if (!searchCache_->get(identity, resultItem))
    {
        LOG(INFO) << "cache miss, begin do search";
        // Get and aggregate keyword search results from mutliple nodes
        distResultItem.setStartCount(actionItem.pageInfo_);

        ret = ro_searchAggregator_->distributeRequest(
                actionItem.collectionName_, request_index, "getDistSearchResult", actionItem, distResultItem);
        if (!ret)
        {
            LOG(ERROR) << "got dist search result failed.";
            return false;
        }
        struct timespec end_time;
        clock_gettime(CLOCK_MONOTONIC, &end_time);
        int interval_ms = (end_time.tv_sec - start_time.tv_sec) * 1000;
        interval_ms += (end_time.tv_nsec - start_time.tv_nsec) / 1000000;

        if (interval_ms > CACHE_THRESHOLD*10)
        {
            LOG(INFO) << "get search result cost too long: " << interval_ms;
        }
        // remove the first topKStart docids.
        if (topKStart > 0)
        {
            if( !distResultItem.topKDocs_.empty() )
            {
                size_t erase_to = std::min(topKStart, distResultItem.topKDocs_.size());
                distResultItem.topKDocs_.erase(distResultItem.topKDocs_.begin(),
                    distResultItem.topKDocs_.begin() + erase_to);
            }
            if( !distResultItem.topKRankScoreList_.empty() )
            {
                size_t erase_to = std::min(topKStart, distResultItem.topKRankScoreList_.size());
                distResultItem.topKRankScoreList_.erase(distResultItem.topKRankScoreList_.begin(),
                    distResultItem.topKRankScoreList_.begin() + erase_to);
            }
            if (!distResultItem.topKCustomRankScoreList_.empty())
            {
                size_t erase_to = std::min(topKStart, distResultItem.topKCustomRankScoreList_.size());
                distResultItem.topKCustomRankScoreList_.erase(distResultItem.topKCustomRankScoreList_.begin(),
                    distResultItem.topKCustomRankScoreList_.begin() + erase_to);
            }
            if (!distResultItem.topKGeoDistanceList_.empty())
            {
                size_t erase_to = std::min(topKStart, distResultItem.topKGeoDistanceList_.size());
                distResultItem.topKGeoDistanceList_.erase(distResultItem.topKGeoDistanceList_.begin(),
                    distResultItem.topKGeoDistanceList_.begin() + erase_to);
            }
        }

        distResultItem.adjustStartCount(topKStart);

        resultItem.swap(distResultItem);
        resultItem.distSearchInfo_.nodeType_ = DistKeywordSearchInfo::NODE_MASTER;

        searchWorker_->rerank(actionItem, resultItem);

        if (actionItem.disableGetDocs_ || resultItem.distSearchInfo_.include_summary_data_)
        {
            LOG(INFO) << "getdocs disabled or summary data included, no need get the data from other workers.";
        }
        else
        {
            // Get and aggregate Summary, Mining results from multiple nodes.
            ResultMapT resultMap;
            searchMerger_->splitSearchResultByWorkerid(resultItem, resultMap);
            if (resultMap.empty())
            {
                // empty is meaning we do not need send request to any worker to get 
                // any documents. But we do need to get mining result.
                LOG(INFO) << "empty worker map after split.";
            }
            else
            {
                RequestGroup<KeywordSearchActionItem, KeywordSearchResult> requestGroup;
                for (ResultMapIterT it = resultMap.begin(); it != resultMap.end(); it++)
                {
                    workerid_t workerid = it->first;
                    KeywordSearchResult& subResultItem = it->second;
                    requestGroup.addRequest(workerid, &actionItem, &subResultItem);
                }

                ret = ro_searchAggregator_->distributeRequest(
                    actionItem.collectionName_, request_index, "getSummaryMiningResult", requestGroup, resultItem);
            }
        }
        if (searchCache_ && !resultItem.topKDocs_.empty() && interval_ms > CACHE_THRESHOLD)
            searchCache_->set(identity, resultItem);
    }
    else
    {
        resultItem.setStartCount(actionItem.pageInfo_);
        resultItem.adjustStartCount(topKStart);

        LOG(INFO) << "result.count: " << resultItem.count_ << ", is disableGetDocs_:" << actionItem.disableGetDocs_;

        ResultMapT resultMap;
        searchMerger_->splitSearchResultByWorkerid(resultItem, resultMap);
        if (resultMap.empty())
        {
            LOG(INFO) << "empty worker map after split.";
        }
        else
        {
            RequestGroup<KeywordSearchActionItem, KeywordSearchResult> requestGroup;
            for (ResultMapIterT it = resultMap.begin(); it != resultMap.end(); it++)
            {
                workerid_t workerid = it->first;
                KeywordSearchResult& subResultItem = it->second;
                requestGroup.addRequest(workerid, &actionItem, &subResultItem);
            }
            ret = ro_searchAggregator_->distributeRequest(
              actionItem.collectionName_, request_index, "getSummaryResult", requestGroup, resultItem);
        }

        struct timespec end_time;
        clock_gettime(CLOCK_MONOTONIC, &end_time);
        int interval_ms = (end_time.tv_sec - start_time.tv_sec) * 1000;
        interval_ms += (end_time.tv_nsec - start_time.tv_nsec) / 1000000;

        if (interval_ms > CACHE_THRESHOLD*5)
        {
            LOG(INFO) << "get cached search result cost too long: " << interval_ms;
        }
    }

    LOG(INFO) << "Total count: " << resultItem.totalCount_ << endl;
    LOG(INFO) << "Top K count: " << resultItem.topKDocs_.size() << endl;
    LOG(INFO) << "Page Count: " << resultItem.count_ << endl;
    LOG(INFO) << "Search Finished " << endl;

    REPORT_PROFILE_TO_FILE( "PerformanceQueryResult.SIAProcess" );

    return true;
}

bool IndexSearchService::getDocumentsByIds(
    const GetDocumentsByIdsActionItem& actionItem,
    RawTextResultFromSIA& resultItem
)
{
    if (!bundleConfig_->isMasterAggregator() || !searchAggregator_->isNeedDistribute())
    {
        searchWorker_->getDocumentsByIds(actionItem, resultItem);
        return !resultItem.idList_.empty();
    }
    /// Perform distributed search by aggregator
    typedef std::map<workerid_t, GetDocumentsByIdsActionItem> ActionItemMapT;
    typedef ActionItemMapT::iterator ActionItemMapIterT;

    uint32_t request_index = ++ro_index_;

    ActionItemMapT actionItemMap;
    if (!searchMerger_->splitGetDocsActionItemByWorkerid(actionItem, actionItemMap))
    {
        if (!actionItem.propertyName_.empty() && !actionItem.propertyValueList_.empty())
        {
            LOG(INFO) << "get docs by property value.";
            ro_searchAggregator_->distributeRequest(actionItem.collectionName_, request_index, "getDocumentsByIds", actionItem, resultItem);
        }
        else
        {
            LOG(WARNING) << "split docs failed.";
            actionItem.print();
            return false;
        }
    }
    else
    {
        RequestGroup<GetDocumentsByIdsActionItem, RawTextResultFromSIA> requestGroup;
        for (ActionItemMapIterT it = actionItemMap.begin(); it != actionItemMap.end(); it++)
        {
            workerid_t workerid = it->first;
            GetDocumentsByIdsActionItem& subActionItem = it->second;
            requestGroup.addRequest(workerid, &subActionItem);
        }

        ro_searchAggregator_->distributeRequest(actionItem.collectionName_, request_index, "getDocumentsByIds", requestGroup, resultItem);
    }

    if (!resultItem.error_.empty())
    {
        LOG(ERROR) << "failed to get documents in . " << __FUNCTION__ << std::endl;
        actionItem.print();
    }
    return !resultItem.idList_.empty();
}

bool IndexSearchService::getInternalDocumentId(
    const std::string& collectionName,
    const uint128_t& scdDocumentId,
    uint64_t& internalId
)
{
    internalId = 0;
    if (!bundleConfig_->isMasterAggregator() || !searchAggregator_->isNeedDistribute())
    {
        searchWorker_->getInternalDocumentId(scdDocumentId, internalId);
        internalId = net::aggregator::Util::GetWDocId(searchAggregator_->getLocalWorker(), (uint32_t)internalId);
    }
    else
    {
        uint32_t request_index = ++ro_index_;
        ro_searchAggregator_->distributeRequest<uint128_t, uint64_t>(
                collectionName, request_index, "getInternalDocumentId", scdDocumentId, internalId);
    }

    return (internalId != 0);
}

uint32_t IndexSearchService::getDocNum(const std::string& collection)
{
    if (!bundleConfig_->isMasterAggregator() || !searchAggregator_->isNeedDistribute())
        return searchWorker_->getDocNum();
    else
    {
        uint32_t request_index = ++ro_index_;
        uint32_t total_docs = 0;
        ro_searchAggregator_->distributeRequest(collection, request_index, "getDistDocNum", total_docs);
        return total_docs;
    }
}

uint32_t IndexSearchService::getKeyCount(const std::string& collection, const std::string& property_name)
{
    if (!bundleConfig_->isMasterAggregator() || !searchAggregator_->isNeedDistribute())
        return searchWorker_->getKeyCount(property_name);
    else
    {
        uint32_t request_index = ++ro_index_;
        uint32_t total_docs = 0;
        ro_searchAggregator_->distributeRequest(collection, request_index, "getDistKeyCount", property_name, total_docs);
        return total_docs;
    }
}

}
