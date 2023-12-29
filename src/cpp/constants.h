#include <bitset>
#include <fstream>
#include <iostream>
#include <unordered_map>
#include <set>
#include <sstream>
#include <string>
#include <tuple>
#include <vector>

using std::vector;

/*
 * File to store large, annoying constants for easy loading and lookup.
 */

const vector<unsigned long long> KING_BOARDS = {3508304109721616384, // boards with kings.
                                                3461860738564358144,
                                                3458958027867029504,
                                                3458776608448446464,
                                                3458765269734785024,
                                                3458764561065181184,
                                                3458764516773330944,
                                                3458764514005090304,
                                                3458764513832075264,
                                                3458764513821261824,
                                                3458764513820585984,
                                                3458764513820543744,
                                                3458764513820541104,
                                                3458764513820540939,
                                                219269006857601024,
                                                216366296160272384,
                                                216184876741689344,
                                                216173538028027904,
                                                216172829358424064,
                                                216172785066573824,
                                                216172782298333184,
                                                216172782125318144,
                                                216172782114504704,
                                                216172782113828864,
                                                216172782113786624,
                                                216172782113783984,
                                                216172782113783819,
                                                13704312928600064,
                                                13522893510017024,
                                                13511554796355584,
                                                13510846126751744,
                                                13510801834901504,
                                                13510799066660864,
                                                13510798893645824,
                                                13510798882832384,
                                                13510798882156544,
                                                13510798882114304,
                                                13510798882111664,
                                                13510798882111499,
                                                856519558037504,
                                                845180844376064,
                                                844472174772224,
                                                844427882921984,
                                                844425114681344,
                                                844424941666304,
                                                844424930852864,
                                                844424930177024,
                                                844424930134784,
                                                844424930132144,
                                                844424930131979,
                                                53532472377344,
                                                52823802773504,
                                                52779510923264,
                                                52776742682624,
                                                52776569667584,
                                                52776558854144,
                                                52776558178304,
                                                52776558136064,
                                                52776558133424,
                                                52776558133259,
                                                3345779523584,
                                                3301487673344,
                                                3298719432704,
                                                3298546417664,
                                                3298535604224,
                                                3298534928384,
                                                3298534886144,
                                                3298534883504,
                                                3298534883339,
                                                209111220224,
                                                206342979584,
                                                206169964544,
                                                206159151104,
                                                206158475264,
                                                206158433024,
                                                206158430384,
                                                206158430219,
                                                13069451264,
                                                12896436224,
                                                12885622784,
                                                12884946944,
                                                12884904704,
                                                12884902064,
                                                12884901899,
                                                816840704,
                                                806027264,
                                                805351424,
                                                805309184,
                                                805306544,
                                                805306379,
                                                51052544,
                                                50376704,
                                                50334464,
                                                50331824,
                                                50331659,
                                                3190784,
                                                3148544,
                                                3145904,
                                                3145739,
                                                199424,
                                                196784,
                                                196619,
                                                12464,
                                                12299,
                                                779};