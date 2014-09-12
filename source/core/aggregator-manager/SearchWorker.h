/**
 * @file SearchWorker.h
 * @author Zhongxia Li
 * @date Jul 5, 2011
 * @brief SearchWorker provides index and mining search interfaces.
 */
#ifndef SEARCH_WORKER_H_
#define SEARCH_WORKER_H_

#include <net/aggregator/BindCallProxyBase.h>
#include <query-manager/ActionItem.h>
#include <query-manager/SearchKeywordOperation.h>
#include <la-manager/AnalysisInformation.h>
#include <common/ResultType.h>

#include <util/get.h>
#include <net/aggregator/Typedef.h>

#include <boost/shared_ptr.hpp>

namespace sf1r
{
using izenelib::ir::idmanager::IDManager;

typedef std::vector<std::pair<uint64_t, float> > SimilarDocIdListType;

class IndexBundleConfiguration;
class MiningSearchService;
class User;
class InvertedIndexManager;
class DocumentManager;
class LAManager;
class SearchManager;
class MiningManager;
class QueryIdentity;
class SearchCache;
class QueryPruneFactory;

class SearchWorker : public net::aggregator::BindCallProxyBase<SearchWorker>
{
public:
    SearchWorker(IndexBundleConfiguration* bundleConfig);

    typedef std::vector<std::pair<uint32_t, izenelib::util::UString> > LabelListT;
    typedef std::vector<std::pair<izenelib::util::UString, std::vector<izenelib::util::UString> > > LabelListWithSimT;

    virtual bool bindCallProxy(CallProxyType& proxy)
    {
        BIND_CALL_PROXY_BEGIN(SearchWorker, proxy)
        BIND_CALL_PROXY_2(getDistSearchInfo, KeywordSearchActionItem, DistKeywordSearchInfo)
        BIND_CALL_PROXY_2(getDistSearchResult, KeywordSearchActionItem, KeywordSearchResult)
        BIND_CALL_PROXY_2(getSummaryResult, KeywordSearchActionItem, KeywordSearchResult)
        BIND_CALL_PROXY_2(getSummaryMiningResult, KeywordSearchActionItem, KeywordSearchResult)
        BIND_CALL_PROXY_2(getDocumentsByIds, GetDocumentsByIdsActionItem, RawTextResultFromSIA)
        BIND_CALL_PROXY_2(getInternalDocumentId, uint128_t, uint64_t)
        BIND_CALL_PROXY_2(clickGroupLabel, ClickGroupLabelActionItem, bool)
        BIND_CALL_PROXY_2(visitDoc, uint32_t, bool)
        BIND_CALL_PROXY_3(HookDistributeRequestForSearch, int, std::string, bool)
        BIND_CALL_PROXY_1(getDistDocNum, uint32_t)
        BIND_CALL_PROXY_2(getDistKeyCount, std::string, uint32_t)
        BIND_CALL_PROXY_END()
    }

    /**
     * Worker services (interfaces)
     * 1) add to WorkerServer to provide remote procedure call
     * 2) add Aggregator to provide in-procedure call
     * @{
     */

    /// search
    void getDistSearchInfo(const KeywordSearchActionItem& actionItem, DistKeywordSearchInfo& resultItem);

    void getDistSearchResult(const KeywordSearchActionItem& actionItem, KeywordSearchResult& resultItem);

    void getSummaryResult(const KeywordSearchActionItem& actionItem, KeywordSearchResult& resultItem);

    void getSummaryMiningResult(const KeywordSearchActionItem& actionItem, KeywordSearchResult& resultItem);

    /// documents
    void getDocumentsByIds(const GetDocumentsByIdsActionItem& actionItem, RawTextResultFromSIA& resultItem);

    void getInternalDocumentId(const uint128_t& actionItem, uint64_t& resultItem);

    void clickGroupLabel(const ClickGroupLabelActionItem& actionItem, bool& result);

    void visitDoc(const uint32_t& docId, bool& result);

    void HookDistributeRequestForSearch(int hooktype, const std::string& reqdata, bool& result);

    void makeQueryIdentity(
            QueryIdentity& identity,
            const KeywordSearchActionItem& item,
            int8_t distActionType = 0,
            uint32_t start = 0);

    bool doLocalSearch(const KeywordSearchActionItem& actionItem, KeywordSearchResult& resultItem);

    void reset_all_property_cache();

    void clearSearchCache();

    void clearFilterCache();

    uint32_t getDocNum();
    void  getDistDocNum(uint32_t& total_docs)
    {
        total_docs = getDocNum();
    }
    uint32_t getKeyCount(const std::string& property_name);
    void getDistKeyCount(const std::string& property_name, uint32_t& total_docs)
    {
        total_docs = getKeyCount(property_name);
    }
    void rerank(const KeywordSearchActionItem& actionItem, KeywordSearchResult& resultItem);

private:
    bool getSearchResult_(
            const KeywordSearchActionItem& actionItem,
            KeywordSearchResult& resultItem,
            bool isDistributedSearch = true);

    bool getSearchResult_(
            const KeywordSearchActionItem& actionItem,
            KeywordSearchResult& resultItem,
            QueryIdentity& identity,
            bool isDistributedSearch = true);

    bool getSummaryMiningResult_(
            const KeywordSearchActionItem& actionItem,
            KeywordSearchResult& resultItem,
            bool isDistributedSearch = true);

    bool getSummaryResult_(
            const KeywordSearchActionItem& actionItem,
            KeywordSearchResult& resultItem,
            bool isDistributedSearch = true);

    void analyze_(const std::string& qstr, std::vector<izenelib::util::UString>& results, bool isQA);

    bool buildQuery(
            SearchKeywordOperation& actionOperation,
            std::vector<std::vector<izenelib::util::UString> >& propertyQueryTermList,
            KeywordSearchResult& resultItem,
            PersonalSearchInfo& personalSearchInfo);

    template<typename ActionItemT, typename ResultItemT>
    bool  getResultItem(
            ActionItemT& actionItem,
            const std::vector<sf1r::docid_t>& docsInPage,
            const vector<vector<izenelib::util::UString> >& propertyQueryTermList,
            ResultItemT& resultItem);

private:
    IndexBundleConfiguration* bundleConfig_;

    boost::shared_ptr<LAManager> laManager_;
    boost::shared_ptr<IDManager> idManager_;
    boost::shared_ptr<DocumentManager> documentManager_;
    boost::shared_ptr<InvertedIndexManager> invertedIndexManager_; // not used from search
    boost::shared_ptr<SearchManager> searchManager_;
    boost::shared_ptr<MiningManager> miningManager_;
    boost::shared_ptr<SearchCache> searchCache_;

    AnalysisInfo analysisInfo_;
    boost::shared_ptr<QueryPruneFactory> queryPruneFactory_;


    friend class IndexBundleActivator;
    friend class MiningBundleActivator;
    friend class SearchMasterItemManagerTestFixture;
};

}

#endif /* SEARCH_WORKER_H_ */
