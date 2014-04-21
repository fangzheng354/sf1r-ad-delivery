#ifndef INDEX_BUNDLE_SEARCH_SERVICE_H
#define INDEX_BUNDLE_SEARCH_SERVICE_H

#include "IndexBundleConfiguration.h"

#include <aggregator-manager/SearchAggregator.h>
#include <common/sf1_serialization_types.h>
#include <common/type_defs.h>
#include <common/Status.h>

#include <query-manager/SearchKeywordOperation.h>
#include <query-manager/ActionItem.h>

#include <util/osgi/IService.h>

#include <boost/shared_ptr.hpp>

namespace sf1r
{
using namespace net::aggregator;

class SearchCache;
class SearchMerger;
class SearchWorker;
class AdSearchService;

class IndexSearchService : public ::izenelib::osgi::IService
{
public:
    IndexSearchService(IndexBundleConfiguration* config);

    ~IndexSearchService();

    boost::shared_ptr<SearchAggregator> getSearchAggregator();

    const IndexBundleConfiguration* getBundleConfig();

    void OnUpdateSearchCache();

public:
    bool getSearchResult(KeywordSearchActionItem& actionItem, KeywordSearchResult& resultItem);

    bool getDocumentsByIds(const GetDocumentsByIdsActionItem& actionItem, RawTextResultFromSIA& resultItem);

    bool getInternalDocumentId(const std::string& collectionName, const uint128_t& scdDocumentId, uint64_t& internalId);

    uint32_t getDocNum(const std::string& collection);

    uint32_t getKeyCount(const std::string& collection, const std::string& property_name);

private:
    IndexBundleConfiguration* bundleConfig_;
    boost::shared_ptr<SearchAggregator> searchAggregator_;
    boost::shared_ptr<SearchAggregator> ro_searchAggregator_;
    SearchMerger* searchMerger_;
    boost::shared_ptr<SearchWorker> searchWorker_;
    boost::shared_ptr<AdSearchService> adSearchService_;

    boost::scoped_ptr<SearchCache> searchCache_; // for Master Node
    boost::atomic<uint32_t> ro_index_;

    friend class SearchWorkerController;
    friend class IndexBundleActivator;
    friend class MiningBundleActivator;
    friend class ProductBundleActivator;
    friend class LocalItemFactory;
    friend class RecommendSearchService;
    friend class SearchMasterItemManagerTestFixture;
};

}

#endif
