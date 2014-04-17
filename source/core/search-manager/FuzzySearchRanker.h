/**
 * @file FuzzySearchRanker.h
 * @brief rank the fuzzy search results.
 * @author Vincent Lee, Jun Jiang
 * @date Created 2013-01-11
 */

#ifndef SF1R_FUZZY_SEARCH_RANKER_H
#define SF1R_FUZZY_SEARCH_RANKER_H

#include <common/inttypes.h>
#include <vector>
#include <string>
#include <set>

namespace sf1r
{
class SearchManagerPreProcessor;
class ProductRankingConfig;
class SearchKeywordOperation;
class KeywordSearchActionItem;
class CustomRankManager;
class DistKeywordSearchInfo;

class FuzzySearchRanker
{
public:
    FuzzySearchRanker(SearchManagerPreProcessor& preprocessor);

    void setFuzzyScoreWeight(const ProductRankingConfig& rankConfig);

    void setCustomRankManager(CustomRankManager* customRankManager)
    {
        customRankManager_ = customRankManager;
    }

    typedef std::pair<double, docid_t> ScoreDocId;

    void rankByProductScore(
        const KeywordSearchActionItem& actionItem,
        std::vector<ScoreDocId>& resultList,
        bool isCompare = false);

    /** rank by property value such Price */
    void rankByPropValue(
            const SearchKeywordOperation& actionOperation,
            uint32_t start,
            std::vector<uint32_t>& docid_list,
            std::vector<float>& result_score_list,
            std::vector<float>& custom_score_list,
            DistKeywordSearchInfo& distSearchInfo);

private:
    void getExcludeDocIds_(const std::string& query,
                           std::set<docid_t>& excludeDocIds);

private:
    SearchManagerPreProcessor& preprocessor_;

    score_t fuzzyScoreWeight_;

    CustomRankManager* customRankManager_;
};

} // namespace sf1r

#endif // SF1R_FUZZY_SEARCH_RANKER_H
