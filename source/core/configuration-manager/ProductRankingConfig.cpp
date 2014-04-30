#include "ProductRankingConfig.h"
#include <process/common/CollectionMeta.h>
#include <sstream>

using namespace sf1r;

namespace
{

bool isStringGroup(
    const ProductScoreConfig& scoreConfig,
    const CollectionMeta& collectionMeta,
    std::string& error)
{
    const std::string& propName = scoreConfig.propName;
    if (propName.empty())
        return true;

    const GroupConfigMap& groupConfigMap =
        collectionMeta.miningBundleConfig_->mining_schema_.group_config_map;

    GroupConfigMap::const_iterator it = groupConfigMap.find(propName);
    if (it == groupConfigMap.end())
    {
        error = "Property [" + propName +
            "] in <ProductRanking> is not configured in <Group>.";
        return false;
    }

    const GroupConfig& groupConfig = it->second;
    if (! groupConfig.isStringType())
    {
        error = "Property [" + propName +
            "] in <ProductRanking> is not string type.";
        return false;
    }

    return true;
}

bool isNumericFilter(
    const ProductScoreConfig& scoreConfig,
    const CollectionMeta& collectionMeta,
    std::string& error)
{
    const std::string& propName = scoreConfig.propName;

    const IndexBundleSchema& indexSchema =
        collectionMeta.indexBundleConfig_->indexSchema_;

    const IndexBundleSchema& zambeziIndexSchema =
        collectionMeta.indexBundleConfig_->zambeziConfig_.zambeziIndexSchema;

    PropertyConfig propConfig;
    propConfig.setName(propName);

    IndexBundleSchema::const_iterator propIt = indexSchema.find(propConfig);
    if (propIt != indexSchema.end() && propIt->isRTypeNumeric())
        return true;

    IndexBundleSchema::const_iterator propZIt = zambeziIndexSchema.find(propConfig);
    if (propZIt != zambeziIndexSchema.end() && propZIt->isRTypeNumeric())
        return true;

    error = "Property [" + propName +
        "] in <ProductRanking> is not numeric filter property.";
    return false;
}

bool isNumericFilter(
    const std::vector<ProductScoreConfig>& scoreConfigs,
    const CollectionMeta& collectionMeta,
    std::string& error)
{
    for (std::vector<ProductScoreConfig>::const_iterator it =
             scoreConfigs.begin(); it != scoreConfigs.end(); ++it)
    {
        if (!isNumericFilter(*it, collectionMeta, error))
            return false;
    }

    return true;
}

bool checkScoreConfig(
    const ProductScoreConfig& scoreConfig,
    const CollectionMeta& collectionMeta,
    std::string& error)
{
    switch(scoreConfig.type)
    {
    case MERCHANT_SCORE:
    case CATEGORY_SCORE:
    case DIVERSITY_SCORE:
        return isStringGroup(scoreConfig, collectionMeta, error);

    case POPULARITY_SCORE:
        return isNumericFilter(scoreConfig.factors, collectionMeta, error);

    case OFFER_ITEM_COUNT_SCORE:
        return isNumericFilter(scoreConfig, collectionMeta, error);

    default:
        return true;
    }
}

} // namespace

const std::string ProductRankingConfig::kScoreTypeName[] =
{
    "merchant",     // MERCHANT_SCORE
    "custom",       // CUSTOM_SCORE
    "category",     // CATEGORY_SCORE
    "relevance",    // RELEVANCE_SCORE
    "popularity",   // POPULARITY_SCORE
    "fuzzy",        // FUZZY_SCORE
    "offer_item_count", // OFFER_ITEM_COUNT_SCORE
    "diversity",    // DIVERSITY_SCORE
    "random",       // RANDOM_SCORE
    "title_relevance", // TITLE_RELEVANCE_SCORE
    "zambezi"       // ZAMBEZI_RELEVANCE_SCORE
};

ProductRankingConfig::ProductRankingConfig()
    : isEnable(false)
    , isDebug(false)
    , scores(PRODUCT_SCORE_NUM)
{
    for (int i = 0; i < PRODUCT_SCORE_NUM; ++i)
    {
        ProductScoreType typeId = static_cast<ProductScoreType>(i);
        scores[i].type = typeId;

        const std::string& typeName = kScoreTypeName[i];
        scoreTypeMap[typeName] = typeId;
    }
}

ProductScoreType ProductRankingConfig::getScoreType(const std::string& typeName) const
{
    ScoreTypeMap::const_iterator it = scoreTypeMap.find(typeName);

    if (it != scoreTypeMap.end())
        return it->second;

    return PRODUCT_SCORE_NUM;
}

bool ProductRankingConfig::checkConfig(
    const CollectionMeta& collectionMeta,
    std::string& error) const
{
    for (std::vector<ProductScoreConfig>::const_iterator it = scores.begin();
         it != scores.end(); ++it)
    {
        if (!checkScoreConfig(*it, collectionMeta, error))
            return false;
    }

    return true;
}

std::string ProductRankingConfig::toStr() const
{
    std::ostringstream oss;

    oss << "ProductRankingConfig::isEnable: " << isEnable
        << ", isDebug: " << isDebug << std::endl;

    for (std::vector<ProductScoreConfig>::const_iterator it = scores.begin();
         it != scores.end(); ++it)
    {
        oss << it->toStr();
    }

    return oss.str();
}
