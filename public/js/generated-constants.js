// Auto-generated from squad-layers-full.md — do not edit manually

export const MAP_CN = {
  'AlBasrah': 'Al Basrah',
  'Anvil': 'Anvil',
  'Belaya': 'Belaya Pass',
  'BlackCoast': 'Black Coast',
  'Chora': 'Chora',
  'Fallujah': 'Fallujah',
  'Fool\sRoad': 'Fool\'s Road',
  'GooseBay': 'Goose Bay',
  'Gorodok': 'Gorodok',
  'Harju': 'Harju',
  'Kamdesh': 'Kamdesh Highlands',
  'Kohat': 'Kohat Toi',
  'Kokan': 'Kokan',
  'Lashkar': 'Lashkar Valley',
  'Logar': 'Logar Valley',
  'Manicouagan': 'Manicouagan',
  'Mestia': 'Mestia',
  'Mutaha': 'Mutaha',
  'Narva': 'Narva',
  'OFL': 'Operation First Light',
  'Sanxian': 'Sanxian Islands',
  'Skorpo': 'Skorpo',
  'Sumari': 'Sumari Bala',
  'Tallil': 'Tallil Outskirts',
  'Yehorivka': 'Yehorivka',
};

export const MODE_CN = {
  "AAS": "突袭/控制",
  "RAAS": "随机突袭",
  "Invasion": "入侵",
  "Insurgency": "叛乱",
  "Skirmish": "遭遇战",
  "Seed": "种子服",
  "TC": "占领控制",
  "Destruction": "破坏",
  "FT": "训练",
  "Tanks": "坦克",
  "TA": "赛道竞速",
  "Training": "训练"
};

export const ROLE_CN = {
  "CombinedArms": "合成",
  "Armored": "装甲",
  "Mechanized": "机械化",
  "Motorized": "摩托化",
  "AirAssault": "空突",
  "LightInfantry": "轻步兵",
  "Support": "支援",
  "AmphibiousAssault": "两栖"
};

export const FACTION_CN = {
  "USA": "美国陆军",
  "USMC": "美国海军陆战队",
  "BAF": "英国陆军",
  "CAF": "加拿大军队",
  "ADF": "澳大利亚国防军",
  "RGF": "俄罗斯联邦陆军",
  "VDV": "俄罗斯空降兵",
  "PLA": "中国人民解放军",
  "PLANMC": "解放军海军陆战队",
  "PLAAGF": "解放军两栖部队",
  "TLF": "土耳其陆军",
  "GFI": "伊朗地面部队",
  "IMF": "伊拉克民兵阵线",
  "MEI": "中东叛军",
  "INS": "叛军武装",
  "WPMC": "瓦格纳雇佣军",
  "CRF": "CRF民兵",
  "AFU": "乌克兰武装部队"
};

// 阵营完整数据（18个阵营，含编制和可用地图，来源：地图阵营.md / squadmaps.com）
// units = 可用编制代码, maps = 可用地图名列表
export const FACTIONS = {
  'USA': { cn: '美国陆军', units: ["CombinedArms","AirAssault","LightInfantry","Armored","Mechanized","Support"], maps: ["Al Basrah","Anvil","Belaya Pass","Black Coast","Chora","Fallujah","Fool's Road","Goose Bay","Gorodok","Harju","Kamdesh Highlands","Kohat Toi","Kokan","Lashkar Valley","Logar Valley","Manicouagan","Mestia","Mutaha","Narva","Operation First Light","Sanxian Islands","Skorpo","Sumari Bala","Tallil Outskirts","Yehorivka"] },
  'USMC': { cn: '美国海军陆战队', units: ["CombinedArms","Armored","LightInfantry","Motorized","Support","AmphibiousAssault"], maps: ["Al Basrah","Anvil","Belaya Pass","Black Coast","Chora","Fallujah","Fool's Road","Goose Bay","Gorodok","Harju","Kamdesh Highlands","Kohat Toi","Kokan","Lashkar Valley","Logar Valley","Manicouagan","Mestia","Mutaha","Narva","Operation First Light","Sanxian Islands","Skorpo","Sumari Bala","Tallil Outskirts","Yehorivka"] },
  'BAF': { cn: '英国陆军', units: ["CombinedArms","AirAssault","Armored","Mechanized","Support"], maps: ["Al Basrah","Anvil","Belaya Pass","Black Coast","Chora","Fallujah","Fool's Road","Goose Bay","Gorodok","Harju","Kamdesh Highlands","Kohat Toi","Kokan","Lashkar Valley","Logar Valley","Manicouagan","Mestia","Mutaha","Narva","Operation First Light","Sanxian Islands","Skorpo","Sumari Bala","Tallil Outskirts","Yehorivka"] },
  'CAF': { cn: '加拿大武装部队', units: ["CombinedArms","AirAssault","Armored","Motorized","Support"], maps: ["Al Basrah","Anvil","Belaya Pass","Black Coast","Chora","Fallujah","Fool's Road","Goose Bay","Gorodok","Harju","Kamdesh Highlands","Kohat Toi","Kokan","Lashkar Valley","Logar Valley","Manicouagan","Mestia","Mutaha","Narva","Operation First Light","Sanxian Islands","Skorpo","Sumari Bala","Tallil Outskirts","Yehorivka"] },
  'ADF': { cn: '澳大利亚国防军', units: ["CombinedArms","Motorized","AirAssault"], maps: ["Al Basrah","Anvil","Belaya Pass","Black Coast","Chora","Fallujah","Fool's Road","Goose Bay","Gorodok","Harju","Kamdesh Highlands","Kohat Toi","Kokan","Lashkar Valley","Logar Valley","Manicouagan","Mestia","Mutaha","Narva","Operation First Light","Sanxian Islands","Skorpo","Sumari Bala","Tallil Outskirts","Yehorivka"] },
  'RGF': { cn: '俄罗斯联邦陆军', units: ["CombinedArms","Motorized","Mechanized","Armored","AmphibiousAssault","Support"], maps: ["Al Basrah","Anvil","Belaya Pass","Black Coast","Chora","Fallujah","Fool's Road","Goose Bay","Gorodok","Harju","Kamdesh Highlands","Kohat Toi","Kokan","Lashkar Valley","Logar Valley","Manicouagan","Mestia","Mutaha","Narva","Operation First Light","Sanxian Islands","Skorpo","Sumari Bala","Tallil Outskirts","Yehorivka"] },
  'VDV': { cn: '俄罗斯空降兵', units: ["CombinedArms","AirAssault","Armored","Support"], maps: ["Al Basrah","Anvil","Belaya Pass","Black Coast","Chora","Fallujah","Fool's Road","Goose Bay","Gorodok","Harju","Kamdesh Highlands","Kohat Toi","Kokan","Lashkar Valley","Logar Valley","Manicouagan","Mestia","Mutaha","Narva","Operation First Light","Sanxian Islands","Skorpo","Sumari Bala","Tallil Outskirts","Yehorivka"] },
  'PLA': { cn: '中国人民解放军', units: ["CombinedArms","AirAssault","Armored","LightInfantry","Motorized","Support"], maps: ["Al Basrah","Anvil","Belaya Pass","Black Coast","Chora","Fallujah","Fool's Road","Goose Bay","Gorodok","Harju","Kamdesh Highlands","Kohat Toi","Kokan","Lashkar Valley","Logar Valley","Manicouagan","Mestia","Mutaha","Narva","Operation First Light","Sanxian Islands","Skorpo","Sumari Bala","Tallil Outskirts","Yehorivka"] },
  'PLANMC': { cn: '解放军海军陆战队', units: ["CombinedArms","AirAssault","Armored","AmphibiousAssault","Support"], maps: ["Al Basrah","Anvil","Belaya Pass","Black Coast","Chora","Fallujah","Fool's Road","Goose Bay","Gorodok","Harju","Kamdesh Highlands","Kohat Toi","Kokan","Lashkar Valley","Logar Valley","Manicouagan","Mestia","Mutaha","Narva","Operation First Light","Sanxian Islands","Skorpo","Sumari Bala","Tallil Outskirts","Yehorivka"] },
  'PLAAGF': { cn: '解放军两栖部队', units: ["CombinedArms","Armored","AmphibiousAssault"], maps: ["Al Basrah","Anvil","Belaya Pass","Black Coast","Chora","Fallujah","Fool's Road","Goose Bay","Gorodok","Harju","Kamdesh Highlands","Kohat Toi","Kokan","Lashkar Valley","Logar Valley","Manicouagan","Mestia","Mutaha","Narva","Operation First Light","Sanxian Islands","Skorpo","Sumari Bala","Tallil Outskirts","Yehorivka"] },
  'TLF': { cn: '土耳其陆军', units: ["CombinedArms","AirAssault","Armored","Mechanized","Motorized","Support"], maps: ["Al Basrah","Anvil","Belaya Pass","Black Coast","Chora","Fallujah","Fool's Road","Gorodok","Harju","Kamdesh Highlands","Kohat Toi","Kokan","Lashkar Valley","Logar Valley","Mestia","Mutaha","Narva","Operation First Light","Skorpo","Sumari Bala","Tallil Outskirts","Yehorivka"] },
  'GFI': { cn: '伊朗地面部队', units: ["CombinedArms","AirAssault","Armored","LightInfantry","Mechanized","Support"], maps: ["Al Basrah","Anvil","Belaya Pass","Black Coast","Chora","Fallujah","Fool's Road","Gorodok","Harju","Kamdesh Highlands","Kohat Toi","Kokan","Lashkar Valley","Logar Valley","Mestia","Mutaha","Narva","Operation First Light","Skorpo","Sumari Bala","Tallil Outskirts","Yehorivka"] },
  'IMF': { cn: '伊拉克民兵阵线', units: ["CombinedArms","Armored","LightInfantry","Mechanized","Motorized","Support"], maps: ["Al Basrah","Anvil","Belaya Pass","Black Coast","Chora","Fallujah","Fool's Road","Gorodok","Harju","Kamdesh Highlands","Kohat Toi","Kokan","Lashkar Valley","Logar Valley","Mestia","Mutaha","Narva","Operation First Light","Sanxian Islands","Skorpo","Sumari Bala","Tallil Outskirts","Yehorivka"] },
  'MEI': { cn: '中东叛军', units: ["CombinedArms","Armored","LightInfantry","Motorized","Support","Mechanized"], maps: ["Al Basrah","Anvil","Belaya Pass","Black Coast","Chora","Fallujah","Fool's Road","Gorodok","Harju","Kamdesh Highlands","Kohat Toi","Kokan","Lashkar Valley","Logar Valley","Mestia","Mutaha","Narva","Operation First Light","Skorpo","Sumari Bala","Tallil Outskirts","Yehorivka"] },
  'WPMC': { cn: '瓦格纳雇佣军', units: ["CombinedArms","AirAssault","LightInfantry"], maps: ["Al Basrah","Anvil","Belaya Pass","Black Coast","Chora","Fallujah","Fool's Road","Goose Bay","Gorodok","Harju","Kamdesh Highlands","Kohat Toi","Kokan","Lashkar Valley","Logar Valley","Manicouagan","Mestia","Mutaha","Narva","Operation First Light","Sanxian Islands","Skorpo","Sumari Bala","Tallil Outskirts","Yehorivka"] },
  'CRF': { cn: 'CRF民兵', units: ["CombinedArms"], maps: ["Al Basrah","Anvil","Belaya Pass","Black Coast","Chora","Fallujah","Fool's Road","Goose Bay","Gorodok","Harju","Kamdesh Highlands","Kohat Toi","Kokan","Lashkar Valley","Logar Valley","Manicouagan","Mestia","Mutaha","Narva","Operation First Light","Skorpo","Sumari Bala","Tallil Outskirts","Yehorivka"] },
  'INS': { cn: '叛军武装', units: ["LightInfantry","Motorized"], maps: [] },
  'MEA': { cn: '中东军', units: ["CombinedArms","Mechanized","Armored"], maps: [] },
  'AFU': { cn: '乌克兰武装部队', units: ["CombinedArms","AirAssault","Armored","Mechanized","Motorized","LightInfantry","Support","AmphibiousAssault"], maps: [] },
};

// 地图 → 可用阵营索引（来源：地图阵营.md）
// 用于级联筛选：选了地图后只显示该地图可用的阵营
export const MAP_FACTIONS = {
  "Al Basrah": ["USA","USMC","BAF","CAF","ADF","RGF","VDV","PLA","PLANMC","PLAAGF","TLF","GFI","IMF","MEI","WPMC","CRF"],
  "Anvil": ["USA","USMC","BAF","CAF","ADF","RGF","VDV","PLA","PLANMC","PLAAGF","TLF","GFI","IMF","MEI","WPMC","CRF"],
  "Belaya Pass": ["USA","USMC","BAF","CAF","ADF","RGF","VDV","PLA","PLANMC","PLAAGF","TLF","GFI","IMF","MEI","WPMC","CRF"],
  "Black Coast": ["USA","USMC","BAF","CAF","ADF","RGF","VDV","PLA","PLANMC","PLAAGF","TLF","GFI","IMF","MEI","WPMC","CRF"],
  "Chora": ["USA","USMC","BAF","CAF","ADF","RGF","VDV","PLA","PLANMC","PLAAGF","TLF","GFI","IMF","MEI","WPMC","CRF"],
  "Fallujah": ["USA","USMC","BAF","CAF","ADF","RGF","VDV","PLA","PLANMC","PLAAGF","TLF","GFI","IMF","MEI","WPMC","CRF"],
  "Fool's Road": ["USA","USMC","BAF","CAF","ADF","RGF","VDV","PLA","PLANMC","PLAAGF","TLF","GFI","IMF","MEI","WPMC","CRF"],
  "Goose Bay": ["USA","USMC","BAF","CAF","ADF","RGF","VDV","PLA","PLANMC","PLAAGF","WPMC","CRF"],
  "Gorodok": ["USA","USMC","BAF","CAF","ADF","RGF","VDV","PLA","PLANMC","PLAAGF","TLF","GFI","IMF","MEI","WPMC","CRF"],
  "Harju": ["USA","USMC","BAF","CAF","ADF","RGF","VDV","PLA","PLANMC","PLAAGF","TLF","GFI","IMF","MEI","WPMC","CRF"],
  "Kamdesh Highlands": ["USA","USMC","BAF","CAF","ADF","RGF","VDV","PLA","PLANMC","PLAAGF","TLF","GFI","IMF","MEI","WPMC","CRF"],
  "Kohat Toi": ["USA","USMC","BAF","CAF","ADF","RGF","VDV","PLA","PLANMC","PLAAGF","TLF","GFI","IMF","MEI","WPMC","CRF"],
  "Kokan": ["USA","USMC","BAF","CAF","ADF","RGF","VDV","PLA","PLANMC","PLAAGF","TLF","GFI","IMF","MEI","WPMC","CRF"],
  "Lashkar Valley": ["USA","USMC","BAF","CAF","ADF","RGF","VDV","PLA","PLANMC","PLAAGF","TLF","GFI","IMF","MEI","WPMC","CRF"],
  "Logar Valley": ["USA","USMC","BAF","CAF","ADF","RGF","VDV","PLA","PLANMC","PLAAGF","TLF","GFI","IMF","MEI","WPMC","CRF"],
  "Manicouagan": ["USA","USMC","BAF","CAF","ADF","RGF","VDV","PLA","PLANMC","PLAAGF","WPMC","CRF"],
  "Mestia": ["USA","USMC","BAF","CAF","ADF","RGF","VDV","PLA","PLANMC","PLAAGF","TLF","GFI","IMF","MEI","WPMC","CRF"],
  "Mutaha": ["USA","USMC","BAF","CAF","ADF","RGF","VDV","PLA","PLANMC","PLAAGF","TLF","GFI","IMF","MEI","WPMC","CRF"],
  "Narva": ["USA","USMC","BAF","CAF","ADF","RGF","VDV","PLA","PLANMC","PLAAGF","TLF","GFI","IMF","MEI","WPMC","CRF"],
  "Operation First Light": ["USA","USMC","BAF","CAF","ADF","RGF","VDV","PLA","PLANMC","PLAAGF","TLF","GFI","IMF","MEI","WPMC","CRF"],
  "Sanxian Islands": ["USA","USMC","BAF","CAF","ADF","RGF","VDV","PLA","PLANMC","PLAAGF","WPMC"],
  "Skorpo": ["USA","USMC","BAF","CAF","ADF","RGF","VDV","PLA","PLANMC","PLAAGF","TLF","GFI","IMF","MEI","WPMC","CRF"],
  "Sumari Bala": ["USA","USMC","BAF","CAF","ADF","RGF","VDV","PLA","PLANMC","PLAAGF","TLF","GFI","IMF","MEI","WPMC","CRF"],
  "Tallil Outskirts": ["USA","USMC","BAF","CAF","ADF","RGF","VDV","PLA","PLANMC","PLAAGF","TLF","GFI","IMF","MEI","WPMC","CRF"],
  "Yehorivka": ["USA","USMC","BAF","CAF","ADF","RGF","VDV","PLA","PLANMC","PLAAGF","TLF","GFI","IMF","MEI","WPMC","CRF"],
};
