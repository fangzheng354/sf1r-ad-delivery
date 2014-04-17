#include "ProductRankerFactory.h"
#include "ProductRanker.h"
#include "ProductRankParam.h"
#include "CategoryScoreEvaluator.h"
#include "OfferItemCountEvaluator.h"
#include "DiversityRoundEvaluator.h"
#include "MerchantScoreEvaluator.h"
#include "RandomScoreEvaluator.h"
#include "../group-manager/PropValueTable.h"
#include <configuration-manager/ProductRankingConfig.h>
#include <common/NumericPropertyTableBase.h>
#include <common/QueryNormalizer.h>
#include <memory> // auto_ptr
#include <algorithm> // min

using namespace sf1r;

namespace
{
/** in zambezi mode, under the same relevance score, run merchant diversity */
const score_t kZambeziCategoryScoreWeight = 1;
const score_t kMinClassifyScore = 0.0001;

score_t minCustomCategoryWeight(const ProductRankingConfig& config)
{
    score_t customWeight = config.scores[CUSTOM_SCORE].weight;
    score_t categoryWeight = config.scores[CATEGORY_SCORE].weight;

    if (customWeight == 0)
        return categoryWeight;

    if (categoryWeight == 0)
        return customWeight;

    return std::min(customWeight, categoryWeight);
}

}

ProductRankerFactory::ProductRankerFactory(
    const ProductRankingConfig& config,
    const faceted::PropValueTable* categoryValueTable,
    const boost::shared_ptr<const NumericPropertyTableBase>& offerItemCountTable,
    const faceted::PropValueTable* diversityValueTable,
    const MerchantScoreManager* merchantScoreManager)
    : config_(config)
    , categoryValueTable_(categoryValueTable)
    , offerItemCountTable_(offerItemCountTable)
    , diversityValueTable_(diversityValueTable)
    , merchantScoreManager_(merchantScoreManager)
    , isRandomScoreConfig_(config.scores[RANDOM_SCORE].weight != 0)
{
}

ProductRanker* ProductRankerFactory::createProductRanker(ProductRankParam& param)
{
    const bool diverseInPage = isDiverseInPage_(param);
    std::auto_ptr<ProductRanker> ranker(
        new ProductRanker(param, config_.isDebug));

    addCategoryEvaluator_(*ranker, diverseInPage);

    if (param.isRandomRank_)
    {
        addRandomEvaluator_(*ranker);
    }
    else
    {
        addOfferItemCountEvaluator_(*ranker, diverseInPage);
        addDiversityEvaluator_(*ranker);
        addMerchantScoreEvaluator_(*ranker);
    }

    return ranker.release();
}

bool ProductRankerFactory::isDiverseInPage_(const ProductRankParam& param) const
{
    return QueryNormalizer::get()->isLongQuery(param.query_);
}

void ProductRankerFactory::addCategoryEvaluator_(ProductRanker& ranker, bool isDiverseInPage) const
{
    SearchingMode::SearchingModeType searchMode = ranker.getParam().searchMode_;
    score_t weight = 0;

    if (searchMode == SearchingMode::ZAMBEZI)
    {
        weight = kZambeziCategoryScoreWeight;
        isDiverseInPage = false;
    }
    else if (isDiverseInPage)
    {
        weight = config_.scores[CUSTOM_SCORE].weight;
    }
    else if (searchMode == SearchingMode::SUFFIX_MATCH)
    {
        weight = kMinClassifyScore;
    }
    else
    {
        weight = minCustomCategoryWeight(config_);
    }

    ranker.addEvaluator(new CategoryScoreEvaluator(weight, isDiverseInPage));
}

void ProductRankerFactory::addRandomEvaluator_(ProductRanker& ranker) const
{
    if (!isRandomScoreConfig_)
        return;

    ranker.addEvaluator(new RandomScoreEvaluator);
}

void ProductRankerFactory::addOfferItemCountEvaluator_(ProductRanker& ranker, bool isDiverseInPage) const
{
    SearchingMode::SearchingModeType searchMode = ranker.getParam().searchMode_;

    if (!offerItemCountTable_ ||
        isDiverseInPage ||
        searchMode == SearchingMode::ZAMBEZI)
        return;

   ranker.addEvaluator(new OfferItemCountEvaluator(offerItemCountTable_));
}

void ProductRankerFactory::addDiversityEvaluator_(ProductRanker& ranker) const
{
    if (!diversityValueTable_)
        return;

    ranker.addEvaluator(new DiversityRoundEvaluator(*diversityValueTable_));
}

void ProductRankerFactory::addMerchantScoreEvaluator_(ProductRanker& ranker) const
{
    if (!diversityValueTable_ || !merchantScoreManager_)
        return;

    MerchantScoreEvaluator* merchantScoreEvaluator = categoryValueTable_ ?
        new MerchantScoreEvaluator(*merchantScoreManager_, *categoryValueTable_) :
        new MerchantScoreEvaluator(*merchantScoreManager_);

    ranker.addEvaluator(merchantScoreEvaluator);
}
