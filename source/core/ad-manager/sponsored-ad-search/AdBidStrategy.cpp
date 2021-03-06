/**
* @file  AdBidStrategy.cpp
* @brief Implementation to class AdBidStrategy.
*/

#include <functional>
#include <limits>
#include <algorithm>
#include <utility>
#include <set>
#include <queue>
#include <cmath>
#include <time.h>
#include <cstdlib>
#include <cstdio>
#include <boost/unordered_map.hpp>
#include <boost/multi_array.hpp>
#include "AdBidStrategy.h"

namespace sf1r
{

namespace sponsored
{

static const double E = 2.71828;

AdBidStrategy::AdBidStrategy()
{

}

AdBidStrategy::~AdBidStrategy()
{

}

static bool isZero(double f)
{
    return std::abs(f) < std::numeric_limits<double>::epsilon();
}

struct Point
{
    double x;
    double y;

    Point(double X = 0.0, double Y = 0.0):x(X), y(Y) {}
};

static bool operator<(const Point& p1, const Point& p2)
{
    return (p1.x < p2.x) || (isZero(p1.x - p2.x) && p1.y < p2.y);
}

static bool xsmall(const Point& p1, const Point& p2)
{
    return (p1.x < p2.x) ;
}

//judge the turning direction for line p0->p1->p2 at point p1.
//turn left return 1, turn right return -1, in the same line return 0.
static int turnDirection(const Point& p0, const Point& p1, const Point& p2)
{
    double tmp = (p2.x - p0.x)*(p1.y - p0.y) - (p2.y - p0.y)*(p1.x - p0.x);
    if (tmp < 0) return 1;
    else if (tmp > 0) return -1;
    else return 0;
}

class AnglePredicate
{
public:
    AnglePredicate(const Point& p): base_(p) {}

    bool operator()(const Point& p1, const Point& p2)
    {
        int tmp = turnDirection(base_, p1, p2);
        return (tmp > 0) || (tmp == 0 &&
                             (p1.x - base_.x)*(p1.x - base_.x) + (p1.y - base_.y)*(p1.y - base_.y) < (p2.x - base_.x)*(p2.x - base_.x) + (p2.y - base_.y)*(p2.y - base_.y)
                            );
    }

private:
    Point base_;
};

std::vector<std::pair<int, double> > AdBidStrategy::convexUniformBid( const std::vector<AdQueryStatisticInfo>& qsInfos, int budget)
{
    static const std::vector<std::pair<int, double> > NULLBID_(2, std::make_pair(0, 0.0));

    if (qsInfos.empty())
    {
        return NULLBID_;
    }


    //aggregate landscape
    boost::unordered_map<int, Point> landscape; //(cpc, <cost, clicks>)
    for (std::vector<AdQueryStatisticInfo>::const_iterator cit = qsInfos.begin(); cit != qsInfos.end(); ++cit)
    {
        std::vector<int>::const_iterator cpcit = cit->cpc_.begin();
        std::vector<double>::const_iterator ctrit = cit->ctr_.begin();
        for (; cpcit != cit->cpc_.end() && ctrit != cit->ctr_.end(); ++cpcit, ++ctrit)
        {
            Point& curP = landscape[*cpcit];
            curP.x += (*cpcit) * (*ctrit);
            curP.y += (*ctrit);
        }
    }

    std::vector<struct Point> allPoints;
    allPoints.reserve(landscape.size());
    Point minP(std::numeric_limits<double>::max(), std::numeric_limits<double>::max());
    size_t minI = -1;
    for (boost::unordered_map<int, Point>::const_iterator cit = landscape.begin(); cit != landscape.end(); ++cit)
    {
        if (cit->second.y > 0 && cit->second.x > 0)
        {
            allPoints.push_back(cit->second);

            if (cit->second < minP)
            {
                minP = cit->second;
                minI = allPoints.size() - 1;
            }
        }
    }
    //   landscape.clear();

    if (allPoints.empty())
    {
        std::vector<std::pair<int, double> > minBID_(2, std::make_pair(qsInfos.front().minBid_, 0.5));
        return minBID_;
    }

    std::swap(allPoints[0], allPoints[minI]);

    AnglePredicate ap(minP);
    std::sort(++allPoints.begin(), allPoints.end(), ap);

    //convex hull, Graham's Scan Algorithm.
    std::vector<struct Point> ch;
    int i = 0;
    while (i < 2 && i < (int)allPoints.size())
    {
        //init
        ch.push_back(allPoints[i++]);
    }

    for (; i < (int)allPoints.size(); ++i)
    {
        while(ch.size() >= 2)
        {
            int tmp = turnDirection(ch[ch.size() -2], ch[ch.size() - 1], allPoints[i]);
            if (tmp < 0)
            {
                ch.pop_back();
            }
            else
            {
                break;
            }
        }
        ch.push_back(allPoints[i]);
    }
    allPoints.clear();

    //candidate convex hull points for bid
    std::vector<struct Point> canch;
    int j = 0;
    for (; j < (int)ch.size(); ++j)
    {
        if (j < ((int)ch.size()) - 1 && ch[j] < ch[j+1])
        {
            continue;
        }
        else
        {
            break;
        }
    }

    //upper bipart of convex hull.
    canch.reserve(ch.size() - j + 2);
    canch.push_back(Point()); //zero point.
    if (ch.size()>= 2 && !isZero(ch.front().x - ch.back().x))
    {
        canch.push_back(ch[0]);
    }

    for (int i = ch.size() -1; i >= j; --i)
    {
        canch.push_back(ch[i]);
    }

    ch.clear();

    //convex combination
    std::vector<std::pair<int, double> > bid;
    int minBid = qsInfos.front().minBid_;

    int singleBudget = 1000000000;
    int totalImpression = 0;
    for (std::vector<AdQueryStatisticInfo>::const_iterator cit = qsInfos.begin(); cit != qsInfos.end(); ++cit)
    {
        totalImpression += cit->impression_;
        singleBudget = std::min(cit->minBid_, singleBudget);
    }
    if (totalImpression > 0)
    {
        singleBudget = std::max(singleBudget, (int)(budget / (totalImpression / (double)qsInfos.size())));
    }



    Point budgetPoint(singleBudget, 0.0);
    std::vector<struct Point>::const_iterator uit = std::upper_bound(canch.begin(), canch.end(), budgetPoint, xsmall);
    if (uit == canch.end())
    {
        int mybid = std::max(minBid, singleBudget);
        bid.push_back(std::make_pair(mybid, 0.5));
        bid.push_back(std::make_pair(mybid, 0.5));
    }
    else
    {
        std::vector<struct Point>::const_iterator preit = uit - 1;
        if (isZero(preit->x - singleBudget))
        {
            int mybid = isZero(preit->y) ? 0 : int(preit->x / preit->y);
            mybid = std::max(minBid, mybid);
            bid.push_back(std::make_pair(mybid, 0.5));
            bid.push_back(std::make_pair(mybid, 0.5));
        }
        else
        {
            double p = double((singleBudget - preit->x) / (uit->x - preit->x));
            int prebid = int(isZero(preit->y)? 0 : preit->x / preit->y);
            if(prebid > 0)
                prebid = std::max(prebid, minBid);
            int lastbid = std::max(int(uit->x / uit->y), minBid);

            bid.push_back(std::make_pair(prebid, p));
            bid.push_back(std::make_pair(lastbid, 1.0 - p));
        }
    }

    return bid;
}

int AdBidStrategy::realtimeBidWithRevenueMax(const AdQueryStatisticInfo& qsInfo,  int budgetUsed, int budgetLeft, int vpc /*= 1000*/)
{
    int budget = budgetLeft + budgetUsed;
    if (budget <= 0 || qsInfo.cpc_.empty())
    {
        return 0;
    }

    double U = std::numeric_limits<double>::max();
    int minBid = qsInfo.minBid_;
    if (minBid < 0)
    {
        minBid = 0;
    }
    if (minBid > 0)
    {
        U = ((double)vpc) / minBid;
    }

    double z = ((double)budgetUsed) / budget;

    //efficiency
    double eff = pow(U * E, z) / E;

    for (int i = 0; i < (int)qsInfo.cpc_.size(); ++i)
    {
        double avaiableB = budgetLeft / (qsInfo.ctr_[i] * qsInfo.impression_);
        double myeff = ((double)vpc) / qsInfo.cpc_[i];
        if (eff > myeff && qsInfo.cpc_[i] <= avaiableB)
        {
            eff = myeff;
        }
    }

    for (int i = 0; i < (int)qsInfo.cpc_.size(); ++i)
    {
        double myeff = ((double)vpc) / qsInfo.cpc_[i];
        if (myeff >= eff)
        {
            return qsInfo.cpc_[i];
        }
    }

    return int(vpc / (1 + eff));
}

int AdBidStrategy::realtimeBidWithProfitMax( const AdQueryStatisticInfo& qsInfo, int budgetUsed, int budgetLeft, int vpc /*= 1000*/)
{
    static const double MINEFF = 0.1;

    int budget = budgetLeft + budgetUsed;
    if (budget <= 0 || qsInfo.cpc_.empty())
    {
        return 0;
    }

    double U = std::numeric_limits<double>::max();
    int minBid = qsInfo.minBid_;
    if (minBid < 0)
    {
        minBid = 0;
    }
    if (minBid > 0)
    {
        U = ((double)vpc) / minBid - 1;
    }

    double z = ((double)budgetUsed) / budget;

    //efficiency
    double eff = pow(U * E / MINEFF, z) * MINEFF / E;

    for (int i = 0; i < (int)qsInfo.cpc_.size(); ++i)
    {
        double avaiableB = budgetLeft / (qsInfo.ctr_[i] * qsInfo.impression_);
        double myeff = ((double)vpc) / qsInfo.cpc_[i] - 1;
        if (eff > myeff && qsInfo.cpc_[i] <= avaiableB)
        {
            eff = myeff;
        }
    }

    int maxI = -1;
    double maxV = -1.0;
    for (int i = 0; i < (int)qsInfo.cpc_.size(); ++i)
    {
        double myeff = ((double)vpc) / qsInfo.cpc_[i] - 1;
        if (myeff >= eff)
        {
            double myV = (vpc - qsInfo.cpc_[i]) * qsInfo.ctr_[i];
            if (myV > maxV)
            {
                maxV = myV;
                maxI = i;
            }
        }
    }
    if (maxI >= 0)
    {
        return qsInfo.cpc_[maxI];
    }
    else
        return int(vpc / (1 + eff));
}


class LargerFit
{
public:
    LargerFit(const std::vector<double>& Fit): _fit(Fit) {}
    bool operator()(int l, int r)
    {
        return _fit[l] < _fit[r];
    }

private:
    const std::vector<double>& _fit;
};

static void enumKPRecursive(const std::vector<std::vector<double> >& W, const std::vector<std::vector<double> >& V, int i, std::vector<int>&curSol, double leftB, double curValue, double& maxValue, std::vector<int>& maxSol)
{
    if(i >= (int) W.size())
    {
        if(curValue > maxValue)
        {
            maxValue = curValue;
            maxSol = curSol;
        }
        return;
    }

    //do not choose cur item.
    curSol[i] = -1;
    enumKPRecursive(W, V, i+1, curSol, leftB, curValue, maxValue, maxSol);

    for(int j = 0; j < (int)W[i].size(); ++j)
    {
        if(leftB >= W[i][j])
        {
            curSol[i] = j;
            enumKPRecursive(W, V, i+1, curSol, leftB - W[i][j], curValue + V[i][j], maxValue, maxSol);
        }
    }
}

static std::vector<int> enumKP(const std::vector<std::vector<double> >& W, const std::vector<std::vector<double> >& V, int B)
{
    std::vector<int> curSol(W.size(), -1);
    std::vector<int> maxSol(W.size(), -1);
    double maxValue = 0.0;

    enumKPRecursive(W, V, 0, curSol, B, 0.0, maxValue, maxSol);

    return maxSol;
}

//dynamic programming for knapsack problem
static std::vector<int> dpKP(const std::vector<std::vector<double> >& W, const std::vector<std::vector<double> >& V, int B)
{
    typedef boost::multi_array<int, 2> array_type;
    typedef array_type::index index;
    array_type	S(boost::extents[W.size()][B+1]);
    std::vector<double> F(B + 1, 0.0);

    for(int i = 0; i < (int)W.size(); ++i)
    {
        for(int v = B; v >= 0; --v)
        {
            double maxF = F[v];
            int maxI = -1;
            for(int j = 0; j < (int)W[i].size(); ++j)
            {
                if(v >= W[i][j])
                {
                    double myF = F[v - W[i][j]] + V[i][j];
                    if(myF > maxF)
                    {
                        maxF = myF;
                        maxI = j;
                    }
                }
            }
            F[v] = maxF;
            S[i][v] = maxI;
        }
    }

    //construct solution.
    std::vector<int> Sol(W.size(), -1);
    for(int i = W.size() - 1, v = B; i >= 0; --i)
    {
        Sol[i] = S[i][v];
        if(S[i][v] != -1)
        {
            v = (int)(v - W[i][Sol[i]]);
        }
    }

    return Sol;
}

static void convertIndexToBid(const std::vector<AdQueryStatisticInfo>& qsInfos, const std::vector<int>& bidindex, std::vector<int>& bid)
{
    std::vector<AdQueryStatisticInfo>::const_iterator cit = qsInfos.begin();
    int i = 0;
    int bi = 0;
    for (; cit != qsInfos.end(); ++cit, ++i)
    {
        if (cit->bid_ != -1)
        {
            bid[i] = cit->bid_;
        }
        else
        {
            if (bidindex[bi] != -1)
            {
                bid[i] = cit->cpc_[bidindex[bi]];
            }
            else
            {
                bid[i] = 0;
            }

            ++bi;
        }
    }

}

//for debug.
static double computeValue(const std::vector<AdQueryStatisticInfo>& qsInfos, const std::vector<int>& sol)
{
    double myV = 0.0;
    std::vector<AdQueryStatisticInfo>::const_iterator cit = qsInfos.begin();
    std::vector<int>::const_iterator sit = sol.begin();
    for (; cit != qsInfos.end(); ++cit)
    {
        if (cit->bid_ == -1)
        {
            if (*sit != -1)
            {
                myV += cit->ctr_[*sit] * cit->impression_;
            }
            ++sit;
        }
    }

    return myV;
}

static void checkGABid(const std::vector<AdQueryStatisticInfo>& qsInfos, int budget, std::vector<int>& bid)
{
    bool allZeroFlag = true;
    std::vector<AdQueryStatisticInfo>::const_iterator qsIt = qsInfos.begin();
    for (std::vector<int>::iterator it = bid.begin(); it != bid.end(); ++it, ++qsIt)
    {
        if (*it > 0)
        {
            if (*it < qsIt->minBid_)
            {
                *it = qsIt->minBid_;
            }

            allZeroFlag = false;
        }
    }
    if (!allZeroFlag)
    {
        return;
    }

    int minBid = qsInfos.front().minBid_;
    std::vector<std::pair<int, double> > ubid = AdBidStrategy::convexUniformBid(qsInfos, budget);
    int lastBid = int(ubid.front().first * ubid.front().second + ubid.back().first * ubid.back().second);
    lastBid = std::max(lastBid, minBid);
    for (std::vector<int>::iterator it = bid.begin(); it != bid.end(); ++it)
    {
        *it = lastBid;
    }
}

std::vector<int> AdBidStrategy::geneticBid( const std::vector<AdQueryStatisticInfo>& qsInfos, int budget )
{
    std::vector<int> bid(qsInfos.size(), 0);


    //support for predefined bidding.
    int tmpKeywordNum = 0, tmpBidIndex = 0, tmpBudget = budget;
    for (std::vector<AdQueryStatisticInfo>::const_iterator cit = qsInfos.begin(); cit != qsInfos.end(); ++cit, ++tmpBidIndex)
    {
        if (cit->bid_ == -1)
        {
            ++tmpKeywordNum;
            bid[tmpBidIndex] = 0;
        }
        else
        {
            bid[tmpBidIndex] = cit->bid_;
            int i = 0;
            for (; i <(int)cit->cpc_.size(); ++i)
            {
                if (cit->bid_ >= cit->cpc_[i])
                {
                    break;
                }
            }
            if (i < (int)cit->cpc_.size())
            {
                tmpBudget -= cit->cpc_[i] * cit->ctr_[i] * cit->impression_;
            }
        }
    }

    const int KeywordNum = tmpKeywordNum;
    const int AvaiableBudget = tmpBudget;


    const int MaxAllowedEvolutions = 300 * KeywordNum;
    const int MinAllowedEvolutions = 50 * KeywordNum;
    static const double EndPopulationRate = 0.90;  //when 90% of the population has same fitness value, stop evolute.
    static const int PopulationSize = 40; //must be even
    static const int ElitismSize = 2;
    static const double MinFitVariance = 0.001;  //minimum max fitness variance ratio. variance / fit^2
    static const long long MaxLoopNum = 10000000000;
    static const long long MaxDPSpace = 100000000;


    if (KeywordNum <= 0 || AvaiableBudget <= 0)
    {
        return bid;
    }

    typedef std::vector<std::vector<double> > TQSDataType;
    TQSDataType W(KeywordNum), V(KeywordNum);
    std::vector<int> adP(KeywordNum);  //ad position num for each keyword.
    int kNum = 0;
    for (std::vector<AdQueryStatisticInfo>::const_iterator cit = qsInfos.begin(); cit != qsInfos.end(); ++cit)
    {
        if (cit->bid_ != -1)
        {
            continue;
        }

        const std::vector<int>& cpc = cit->cpc_;
        const std::vector<double>& ctr = cit->ctr_;

        W[kNum].reserve(cpc.size());
        V[kNum].reserve(cpc.size());
        adP[kNum] = cpc.size();

        for (int j = 0; j < (int)cpc.size(); ++j)
        {
            W[kNum].push_back(cpc[j] * cit->impression_ * ctr[j]);
            V[kNum].push_back(cit->impression_ * ctr[j]);  //max traffics. value is defined as click traffics.
        }

        ++kNum;
    }

    // judge whether problem can be solved by enumerating to directly get optimal solution.
    {
        long long timeCP = 1;
        for(int i = 0; i < KeywordNum; ++i)
        {
            timeCP *= (W[i].size() + 1);
            if(timeCP > MaxLoopNum) break;
        }
        if(timeCP < MaxLoopNum)
        {
            const std::vector<int>& mySol = enumKP(W, V, AvaiableBudget);
            convertIndexToBid(qsInfos, mySol, bid);
            checkGABid(qsInfos, budget, bid);
            return bid;
        }
    }

    // judge whether problem can be solved by dynamic programming to directly get optimal solution.
    {
        long long spaceComplexity = KeywordNum;
        spaceComplexity *= (AvaiableBudget + 1);
        if (spaceComplexity <= MaxDPSpace)
        {
            const std::vector<int>& mySol = dpKP(W, V, AvaiableBudget);
            convertIndexToBid(qsInfos, mySol, bid);
            checkGABid(qsInfos, budget, bid);
            return bid;
        }
    }

    typedef std::vector<std::vector<int> > TPopType; //every individual is a vector of index of ad position for each keyword, 0-based.
    TPopType P(PopulationSize);
    TPopType newP(PopulationSize);

    srand(time(NULL));
    for (int i = 0; i < PopulationSize; ++i)
    {
        P[i].reserve(KeywordNum);
        newP[i].reserve(KeywordNum);
        for (size_t j = 0; j < KeywordNum; ++j)
        {
            int N = adP[j] + 1;
            P[i].push_back(rand() % N - 1); //ad position is 0-based, -1 means do not bid for that keyword.
            newP[i].push_back(0);
        }

        //clear population for budget requirement.
        for (int i = 0; i < PopulationSize; ++i)
        {
            std::vector<std::pair<int, int> > kw; //(keyword index, selected ad position index)
            for (int j = 0; j < (int)(P[i].size()); ++j)
            {
                //for each keyword
                if (P[i][j] != -1)
                {
                    kw.push_back(std::make_pair(j, P[i][j]));
                }
            }

            double totalW = 0.0;
            for (std::vector<std::pair<int, int> >::const_iterator cit = kw.begin(); cit != kw.end(); ++cit)
            {
                totalW += W[cit->first][cit->second];
            }

            int aN = kw.size();
            while(totalW > AvaiableBudget)
            {
                int r = rand() % aN;
                totalW -= W[kw[r].first][kw[r].second];
                P[i][kw[r].first] = -1;
                std::swap(kw[r], kw[aN - 1]);
                --aN;
            }
        }
    }

    int iterNum = MaxAllowedEvolutions;
    double averageFit = 0.0, averageSquareFit = 0.0;

    while(iterNum--)
    {
        //selection,
        std::vector<int> SP(PopulationSize); //selected individual's index in P
        int GASize = PopulationSize - ElitismSize;

        {
            std::vector<double> fitness(PopulationSize); //total fitness.
            std::vector<double> fit(PopulationSize);    //each individual fitness.
            double fn = 0.0;
            for (int i = 0; i < PopulationSize; ++i)
            {
                double curfn = 0.0;
                for (int j = 0; j < (int)(P[i].size()); ++j)
                {
                    //for each keyword
                    if (P[i][j] != -1)
                    {
                        curfn += V[j][P[i][j]];
                    }
                }
                fn += curfn;
                fit[i] = curfn;
                fitness[i] = fn;
            }

            //check condition of stop evolution.
            if (MaxAllowedEvolutions - iterNum > MinAllowedEvolutions)
            {
                boost::unordered_map<double, int> fitNum;
                for (int i = 0; i < PopulationSize; ++i)
                {
                    fitNum[fit[i]]++;
                }
                int maxNum = -1;
                for (boost::unordered_map<double, int>::const_iterator cit = fitNum.begin(); cit != fitNum.end(); ++cit)
                {
                    if (cit->second > maxNum)
                    {
                        maxNum = cit->second;
                    }
                }

                if ((double)maxNum / PopulationSize >= EndPopulationRate)
                {
                    break;
                }
            }

            //stochastic universal sampling.
            double fstep = fn / GASize;
            double fstart = ((double)rand()) / RAND_MAX * fstep;

            if (!isZero(fstep))
            {
                for (int i = 0, j = 0; i < GASize; ++i)
                {
                    double fcur = fstart + i * fstep;
                    for (int k = j; k < PopulationSize; ++k)
                    {
                        if (fitness[k] > fcur)
                        {
                            SP[i] = k;
                            j = k;
                            break;
                        }
                    }
                }
            }
            else
            {
                for (int i = 0; i < PopulationSize; ++i)
                {
                    SP[i] = i;
                }
            }

            //random to select crossover pair
            for (int i = GASize; i >= 1; --i)
            {
                int j = rand() % i;
                std::swap(SP[j], SP[i - 1]);
            }

            //elitism
            LargerFit mylf(fit);
            std::priority_queue<int, std::vector<int>, LargerFit> pq(mylf);
            for (int i = 0; i < PopulationSize; ++i)
            {
                pq.push(i);
            }

            {
                //average fit
                int eNum = MaxAllowedEvolutions - iterNum;
                double curMaxFit = fit[pq.top()];
                if (!isZero(curMaxFit))
                {
                    averageFit = ((eNum - 1) * averageFit + curMaxFit) / eNum;
                    averageSquareFit = ((eNum - 1) * averageSquareFit + curMaxFit * curMaxFit) / eNum;
                    //variance
                    double variance = averageSquareFit - averageFit * averageFit;
                    if (eNum > MinAllowedEvolutions && variance / (curMaxFit*curMaxFit) < MinFitVariance)
                    {
                        break;
                    }
                }
            }

            for (int i = GASize; i < PopulationSize; ++i)
            {
                SP[i] = pq.top();
                pq.pop();
            }


        }

        //crossover
        int crossoverRate = 85;
        for (int i = 0; i < GASize; i += 2)
        {
            int myRate = rand() % 100;
            if (myRate < crossoverRate)
            {
                const std::vector<int>& lp = P[SP[i]];
                const std::vector<int>& rp = P[SP[i+1]];

                for (int j = 0; j < KeywordNum; ++j)
                {
                    double p = (double)rand() / RAND_MAX * 1.50 - 0.25;

                    if (lp[j] != -1 && rp[j] != -1)
                    {
                        //newP[i][j]
                        int v = (int)(lp[j] * p + rp[j] * (1.0 - p) + 0.5);
                        v = std::max(v, 0);
                        v = std::min(v, adP[j] - 1);
                        newP[i][j] = v;

                        //newP[i+1]
                        //newP[i]
                        v = (int)(lp[j] * ( 1.0 - p ) + rp[j] * p + 0.5);
                        v = std::max(v, 0);
                        v = std::min(v, adP[j] - 1);
                        newP[i+1][j] = v;
                    }
                    else if (lp[j] == -1 && rp[j] == -1)
                    {
                        newP[i][j] = -1;
                        newP[i+1][j] = -1;
                    }
                    else
                    {
                        //discrete
                        int ip = rand() % 2;
                        if (ip)
                        {
                            newP[i][j] = -1;
                            newP[i+1][j] = -1;
                        }
                        else
                        {
                            int tmp = std::max(lp[j], rp[j]);
                            newP[i][j] = tmp;
                            newP[i+1][j] = tmp;
                        }
                    }

                }
            }
            else
            {
                newP[i] = P[SP[i]];
                newP[i+1] = P[SP[i+1]];
            }
        }
        for (int i = GASize; i < PopulationSize; ++i)
        {
            newP[i] = P[SP[i]];
        }

        //mutation, mutation rate, 1/countof(var)
        int mRate = KeywordNum;
        for (int i = 0; i < GASize; ++i)
        {
            for (int j = 0; j < KeywordNum; ++j)
            {
                if (rand() % mRate == 0)
                {
                    //do mutation
                    newP[i][j] = rand() % (adP[j] + 1) - 1;
                }
            }
        }

        newP.swap(P);


        //clear population for budget requirement.
        for (int i = 0; i < PopulationSize; ++i)
        {
            std::vector<std::pair<int, int> > kw; //(keyword index, selected ad position index)
            for (int j = 0; j < (int)(P[i].size()); ++j)
            {
                //for each keyword
                if (P[i][j] != -1)
                {
                    kw.push_back(std::make_pair(j, P[i][j]));
                }
            }

            double totalW = 0.0;
            for (std::vector<std::pair<int, int> >::const_iterator cit = kw.begin(); cit != kw.end(); ++cit)
            {
                totalW += W[cit->first][cit->second];
            }

            int aN = kw.size();
            while(totalW > AvaiableBudget)
            {
                int r = rand() % aN;
                totalW -= W[kw[r].first][kw[r].second];
                P[i][kw[r].first] = -1;
                std::swap(kw[r], kw[aN - 1]);
                --aN;
            }
        }

    }

    //max fitness in population
    int maxI = -1;
    double maxfit = -1.0;
    for (int i = 0; i < PopulationSize; ++i)
    {
        double ft = 0.0;
        for (int j = 0; j < KeywordNum; ++j)
        {
            if (P[i][j] != -1)
            {
                ft += V[j][P[i][j]];
            }
        }
        if (ft > maxfit)
        {
            maxfit = ft;
            maxI = i;
        }
    }


    if (maxI != -1)
    {
        convertIndexToBid(qsInfos, P[maxI], bid);

    }

    checkGABid(qsInfos, budget, bid);
    return bid;
}

}
}

