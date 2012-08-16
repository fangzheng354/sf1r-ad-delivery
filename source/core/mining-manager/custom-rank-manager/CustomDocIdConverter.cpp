#include "CustomDocIdConverter.h"
#include <common/Utilities.h>
#include <util/ClockTimer.h>

namespace sf1r
{

bool CustomDocIdConverter::convert(
    const CustomRankDocStr& customDocStr,
    CustomRankDocId& customDocId
)
{
    izenelib::util::ClockTimer timer;

    bool result = convertDocIdList_(customDocStr.topIds, customDocId.topIds) &&
                  convertDocIdList_(customDocStr.excludeIds, customDocId.excludeIds);

    LOG(INFO) << "top num: " << customDocId.topIds.size()
              << ", exclude num: " << customDocId.excludeIds.size()
              << ", id conversion costs: " << timer.elapsed() << " seconds";

    return result;
}

bool CustomDocIdConverter::convertDocIdList_(
    const std::vector<std::string>& docStrList,
    std::vector<docid_t>& docIdList
)
{
    docid_t docId = 0;

    for (std::vector<std::string>::const_iterator it = docStrList.begin();
        it != docStrList.end(); ++it)
    {
        if (! convertDocId_(*it, docId))
            return false;

        docIdList.push_back(docId);
    }

    return true;
}

bool CustomDocIdConverter::convertDocId_(
    const std::string& docStr,
    docid_t& docId
)
{
    uint128_t convertId = Utilities::md5ToUint128(docStr);

    if (! idManager_.getDocIdByDocName(convertId, docId, false))
    {
        LOG(WARNING) << "in convertDocId_(), DOCID " << docStr
                     << " does not exist";
        return false;
    }

    return true;
}

} // namespace sf1r