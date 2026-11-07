#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <cstdio>

#include "../../../src/xcore/xcore.h"
#include "../../../src/libxpeccy/cpu/Z80/z80.h"

xConfig conf;

QString gethexbyte(int value) { return QString("%1").arg(value & 0xff, 2, 16, QChar('0')).toUpper(); }
QString gethexword(int value) { return QString("%1").arg(value & 0xffff, 4, 16, QChar('0')).toUpper(); }
QString xconf_install_dir() { return QString(); }

static int failures;

static void check(const char* name, bool ok) {
	std::printf("%s: %s\n", name, ok ? "ok" : "FAIL");
	if (!ok) failures++;
}

static xResult eval(const char* expression, int type = 0) {
	return xEval(expression, type);
}

int main(int argc, char** argv) {
	QCoreApplication app(argc, argv);
	if (argc != 2) return 2;
	QDir fixtures(QString::fromLocal8Bit(argv[1]));
	QTemporaryDir temp;
	if (!temp.isValid()) return 2;
	Computer* comp = compCreate();
	conf.zx = comp;
	compSetHardware(comp, "ZXNext");
	compSetBaseFrq(comp, 3.5);
	compReset(comp, RES_DEFAULT);
	comp->flgBOOT = 0;
	comp->next->configMode = 0;
	comp->next->nr[0x83] = 0;
	check("load Next labels", loadLabels(fixtures.filePath("next.labels").toLocal8Bit().constData()) == 1);
	xAdr camera = find_label("Camera.WORLD_X");
	xAdr odd = find_label("Debug.calc_char");
	check("8K pages and SRAM base", camera.type == MEM_RAM && camera.abs == 0x48456
		&& odd.abs == 0x4a039 && find_label("LastRAM").abs == 0x1fffff);
	check("pages are decimal", find_label("Page17").abs == 0x40000 + 17 * 0x2000 + 0x123);
	check("adjacent pages stay distinct", find_label("EvenPage").abs == 0x48200
		&& find_label("OddPage").abs == 0x4a200 && find_label("LastByte").abs + 1 == find_label("NextPage").abs);
	check("CPU label lookup", camera.adr == 0x8456 && odd.adr == 0xa039
		&& find_label(comp_get_xadr(comp, 0x8456, MEM_VIEW_READ)) == "Camera.WORLD_X");
	comp->mem->ramData[camera.abs] = 0x42;
	check("CPU expression reads the label", !eval("Camera.WORLD_X").err && eval("Camera.WORLD_X").value == 0x8456
		&& eval("M(Camera.WORLD_X)").value == 0x42);
	xExpr condition = xexpr_compile("PC == Camera.WORLD_X");
	next_mmu_set(comp, 4, 0x20);
	next_mmu_set(comp, 6, 4);
	comp->cpu->regPC = 0xc456;
	check("expressions follow MMU changes", find_label("Camera.WORLD_X").adr == 0xc456
		&& eval("Camera.WORLD_X").value == 0xc456 && xexpr_eval(condition, comp) == 1);
	next_mmu_set(comp, 6, 0x21);
	check("unmapped CPU label reports an error", find_label("Camera.WORLD_X").adr == -1
		&& eval("Camera.WORLD_X").err != 0);
	xResult physical = eval("Camera.WORLD_X", MEM_RAM);
	check("physical RAM expression survives unmapping", !physical.err && physical.value == 0x48456
		&& eval("M(Camera.WORLD_X)", MEM_RAM).value == 0x42);
	check("virtual labels stay at their CPU address", find_label("Virtual").adr == 0x8123
		&& find_label(comp_get_xadr(comp, 0x8123, MEM_VIEW_READ)) == "Virtual");
	comp->next->nr[0x83] = 1;
	comp->next->pe3 = 0x82;
	comp->mem->ramData[0x24123] = 0x5a;
	check("expression memory reads follow overlays", !eval("M(0x2123)").err && eval("M(0x2123)").value == 0x5a);
	next_mmu_set(comp, 0, 4);
	check("hidden MMU pages are not CPU addresses", find_label("Camera.WORLD_X").adr == -1);
	comp->next->nr[0x83] = 0;
	check("exposed MMU page becomes visible", find_label("Camera.WORLD_X").adr == 0x456);
	comp->next->p123b = 4;
	comp->next->nr[0x12] = 0x20;
	check("Layer 2 overlay hides an MMU page", find_label("Camera.WORLD_X").adr == -1);
	comp->next->p123b = 0;
	// labels the debugger puts on ROM and divMMC cells, kept by their area
	xAdr cell = {MEM_RAM, 0, 0x0038, 0x0038};
	add_label(cell, "RomLabel");
	cell.abs = 0x20000 + 3 * 0x2000 + 0x10;
	cell.adr = -1;
	add_label(cell, "DivLabel");
	// a CPU label names the cell at its address, not the same offset of a page
	cell.type = MEM_IO;
	cell.abs = cell.adr = 0x0123;
	add_label(cell, "LowCpu");
	next_mmu_set(comp, 0, 0xff);
	xAdr page = {MEM_RAM, 0, 0x0123, 0x40000 + 0x30 * 0x2000 + 0x123};
	check("CPU labels stay off page views", find_label(page).isEmpty()
		&& find_label(comp_get_xadr(comp, 0x0123, MEM_VIEW_READ)) == "LowCpu");
	QMap<QString, xAdr> before = conf.curlabset->list;
	QString saved = temp.filePath("roundtrip.labels");
	check("save Next labels", saveLabels(saved.toLocal8Bit().constData()) == 1);
	QFile file(saved);
	file.open(QIODevice::ReadOnly);
	QByteArray text = file.readAll();
	file.close();
	check("export keeps Next numbering", text.contains("04:0456 Camera.WORLD_X\n")
		&& text.contains("05:0039 Debug.calc_char\n") && text.contains(":8123 Virtual\n")
		&& text.contains("17:0123 Page17\n") && text.contains("223:1FFF LastRAM\n"));
	check("export names ROM and divMMC cells", text.contains("ROM0:0038 RomLabel\n") && text.contains("DIV3:0010 DivLabel\n")
		&& text.contains(":0123 LowCpu\n"));
	loadLabels(saved.toLocal8Bit().constData());
	bool same = conf.curlabset->list.size() == before.size();
	for (auto it = before.begin(); it != before.end(); ++it) {
		xAdr after = find_label(it.key());
		same &= after.type == it.value().type && after.abs == it.value().abs;
	}
	check("Next labels round trip", same);
	// SjASMPlus's own output (sjasm-next.asm says how it was made)
	check("load SjASMPlus labels", loadLabels(fixtures.filePath("sjasm-next.labels").toLocal8Bit().constData()) == 1);
	check("SjASMPlus pages", find_label("start").abs == 0x40000 + 4 * 0x2000
		&& find_label("lowpage").abs == 0x40000 + 10 * 0x2000
		&& find_label("dot").abs == 0x40000 + 11 * 0x2000
		&& find_label("p17b").abs == 0x40000 + 17 * 0x2000 + 0x123
		&& find_label("last").abs == 0x40000 + 223 * 0x2000);
	check("load SjASMPlus map", loadLabels(fixtures.filePath("sjasm-next.map").toLocal8Bit().constData()) == 1);
	check("map places by linear address", find_label("P17B").abs == 0x40000 + 17 * 0x2000 + 0x123
		&& find_label("DOT").abs == 0x40000 + 11 * 0x2000 && find_label("LAST").abs == 0x40000 + 223 * 0x2000
		&& find_label("START").abs == 0x40000 + 4 * 0x2000);
	check("map leaves EQU out", find_label("CONST").type < 0 && conf.curlabset->list.size() == 6);
	compSetHardware(comp, "ZX128");
	compReset(comp, RES_DEFAULT);
	loadLabels(fixtures.filePath("legacy.labels").toLocal8Bit().constData());
	xAdr legacy = find_label("print_score");
	check("legacy 16K labels stay compatible", legacy.abs == 0x8010 && legacy.adr == 0x8010
		&& find_label("screen_data").abs == 0x14010 && find_label("level_data").abs == 0x1a00);
	check("16K pages: decimal, and hex from older Xpeccy+ files",
		find_label("decimal_bank").abs == ((12 << 14) | 0x20) && find_label("old_hex_bank").abs == ((10 << 14) | 0x10));
	loadLabels(fixtures.filePath("sjasm-128.labels").toLocal8Bit().constData());
	check("SjASMPlus 128K labels", find_label("b7").abs == ((7 << 14) | 0x123) && find_label("b7").adr == 0xc123
		&& find_label("scr").abs == ((5 << 14) | 0x10) && find_label("scr").adr == 0x4010);
	loadLabels(fixtures.filePath("sjasm-128.map").toLocal8Bit().constData());
	check("SjASMPlus 128K map", find_label("B7").abs == ((7 << 14) | 0x123) && find_label("SCR").adr == 0x4010
		&& find_label("CONST").type < 0);
	loadLabels(fixtures.filePath("legacy.labels").toLocal8Bit().constData());
	xAdr rom0 = {MEM_ROM, 0, 0x0038, 0x38};	// the bench machine has no ROM file: one page
	add_label(rom0, "RomLabel");
	QString saved128 = temp.filePath("z128.labels");
	saveLabels(saved128.toLocal8Bit().constData());
	QFile f128(saved128);
	f128.open(QIODevice::ReadOnly);
	QByteArray text128 = f128.readAll();
	f128.close();
	loadLabels(saved128.toLocal8Bit().constData());
	check("16K ROM labels by name", text128.contains("ROM0:0038 RomLabel\n") && text128.contains("02:0010 print_score\n")
		&& find_label("RomLabel").type == MEM_ROM && find_label("RomLabel").abs == 0x38);
	loadLabels(fixtures.filePath("legacy.labels").toLocal8Bit().constData());
	check("legacy label expressions", !eval("print_score").err && eval("print_score").value == 0x8010);
	while (!conf.labsets.isEmpty()) delLabelSet(conf.labsets.first()->name);
	compDestroy(comp);
	conf.zx = nullptr;
	return failures ? 1 : 0;
}
