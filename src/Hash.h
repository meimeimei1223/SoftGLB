#ifndef HASH_H
#define HASH_H

#include <vector>
#include <cmath>
#include <algorithm>
#include <iostream>

class Hash {
public:
    float spacing;
    int tableSize;
    std::vector<int> cellStart;
    std::vector<int> cellEntries;
    std::vector<int> queryIds;
    int querySize;
    int numPoints;  // 追加：点の総数を保持
    /*
    Hash(float spacing, int maxNumObjects)
        : spacing(spacing), tableSize(2 * maxNumObjects), querySize(0) {
        cellStart.resize(tableSize + 1, 0);
        cellEntries.resize(maxNumObjects, 0);
        queryIds.resize(maxNumObjects, 0);
    }
    */
    Hash(float spacing, int maxNumObjects)
            : spacing(spacing)
            , tableSize(2 * maxNumObjects)
            , querySize(0)
            , numPoints(maxNumObjects) {  // numPointsの初期化を追加
            cellStart.resize(tableSize + 1, 0);
            cellEntries.resize(maxNumObjects, 0);
            queryIds.resize(maxNumObjects, 0);
        }

    // ハッシュ関数
    int hashCoords(int xi, int yi, int zi) {
        int h = (xi * 92837111) ^ (yi * 689287499) ^ (zi * 283923481); // ファンタジー関数
        return std::abs(h) % tableSize;
    }

    // 座標を整数セルに変換
    int intCoord(float coord) {
        return static_cast<int>(std::floor(coord / spacing));
    }

    // 位置情報をハッシュ値に変換
    int hashPos(const std::vector<float>& pos, int nr) {
        return hashCoords(
            intCoord(pos[3 * nr]),
            intCoord(pos[3 * nr + 1]),
            intCoord(pos[3 * nr + 2])
        );
    }

    // ハッシュテーブルの作成
    void create(const std::vector<float>& pos) {
        int numObjects = std::min(static_cast<int>(pos.size() / 3), static_cast<int>(cellEntries.size()));

        std::fill(cellStart.begin(), cellStart.end(), 0);
        std::fill(cellEntries.begin(), cellEntries.end(), 0);

        for (int i = 0; i < numObjects; i++) {
            int h = hashPos(pos, i);
            cellStart[h]++;
        }

        // セルの開始位置を決定
        int start = 0;
        for (int i = 0; i < tableSize; i++) {
            start += cellStart[i];
            cellStart[i] = start;
        }
        cellStart[tableSize] = start; // ガード

        // オブジェクトIDを格納
        for (int i = 0; i < numObjects; i++) {
            int h = hashPos(pos, i);
            cellStart[h]--;
            cellEntries[cellStart[h]] = i;
        }
    }
    void query(const std::vector<float>& pos, int nr, float maxDist) {
        // maxDistの制限
        const float MAX_SEARCH_DIST = 5.0f;
        if (maxDist > MAX_SEARCH_DIST) {
            std::cout << "Warning: Search distance limited to " << MAX_SEARCH_DIST << std::endl;
            maxDist = MAX_SEARCH_DIST;
        }

        // セルの範囲を計算
        int x0 = intCoord(pos[3 * nr] - maxDist);
        int y0 = intCoord(pos[3 * nr + 1] - maxDist);
        int z0 = intCoord(pos[3 * nr + 2] - maxDist);
        int x1 = intCoord(pos[3 * nr] + maxDist);
        int y1 = intCoord(pos[3 * nr + 1] + maxDist);
        int z1 = intCoord(pos[3 * nr + 2] + maxDist);

        // セル範囲のチェック
        const int MAX_CELL_RANGE = 20;
        if (x1 - x0 > MAX_CELL_RANGE || y1 - y0 > MAX_CELL_RANGE || z1 - z0 > MAX_CELL_RANGE) {
            std::cout << "Warning: Cell range too large, search limited" << std::endl;
            querySize = 0;
            return;
        }


            querySize = 0;

            for (int xi = x0; xi <= x1; xi++) {
                for (int yi = y0; yi <= y1; yi++) {
                    for (int zi = z0; zi <= z1; zi++) {
                        int h = hashCoords(xi, yi, zi);
                        if (h < 0 || h >= tableSize) continue;

                        int start = cellStart[h];
                        int end = cellStart[h + 1];

                        for (int i = start; i < end && querySize < queryIds.size(); i++) {
                            int id = cellEntries[i];
                            if (id >= 0 && id < numPoints) {  // 範囲チェックを追加
                                queryIds[querySize++] = id;
                            }
                        }
                    }
                }
            }
        }



    /*
    // 指定範囲内のオブジェクトをクエリ
    void query(const std::vector<float>& pos, int nr, float maxDist) {
        int x0 = intCoord(pos[3 * nr] - maxDist);
        int y0 = intCoord(pos[3 * nr + 1] - maxDist);
        int z0 = intCoord(pos[3 * nr + 2] - maxDist);

        int x1 = intCoord(pos[3 * nr] + maxDist);
        int y1 = intCoord(pos[3 * nr + 1] + maxDist);
        int z1 = intCoord(pos[3 * nr + 2] + maxDist);

        querySize = 0;

        for (int xi = x0; xi <= x1; xi++) {
            for (int yi = y0; yi <= y1; yi++) {
                for (int zi = z0; zi <= z1; zi++) {
                    int h = hashCoords(xi, yi, zi);
                    int start = cellStart[h];
                    int end = cellStart[h + 1];

                    for (int i = start; i < end; i++) {
                        queryIds[querySize++] = cellEntries[i];
                    }
                }
            }
        }
    }
    */
};

#endif // HASH_H
