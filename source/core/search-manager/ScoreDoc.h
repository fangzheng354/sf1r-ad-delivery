#ifndef SF1R_SEARCH_MANAGER_SCORE_DOC_H
#define SF1R_SEARCH_MANAGER_SCORE_DOC_H
/**
 * @file sf1r/search-manager/ScoreDoc.h
 * @author Ian Yang
 * @date Created <2010-03-24 15:34:32>
 */
#include <common/inttypes.h>
namespace sf1r
{
struct ScoreDoc
{
    explicit ScoreDoc(docid_t id = 0, double s = 0.0)
        : docId(id), score(s), custom_score(0.0), geo_dist(0.0)
    {}

    ~ScoreDoc()
    {
    }

    docid_t docId;
    double score;
    double custom_score;
    double geo_dist;
};
} // namespace sf1r
#endif // SF1R_SEARCH_MANAGER_SCORE_DOC_H
