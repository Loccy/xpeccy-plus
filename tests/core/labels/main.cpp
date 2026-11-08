#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <cstdio>

#include "../../../src/xcore/xcore.h"
#include "../../../src/libxpeccy/cpu/Z80/z80.h"

xConfig conf;

QString gethexbyte(int value) { return QString("%1").arg(value & 0xff, 2, 16, QChar('0')).toUpper(); }
QString gethexword(int value) { return QString("%1").arg(value & 0xffff, 4, 16, QChar('0')).toUpper(); }
static QString installDir;		// the ROM lists of the test are in its labels/
QString xconf_install_dir() { return installDir; }
QString arc_beside(const QString& path, const QString& ext) { return path + ext; }	// labels_image: unused here

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
	// ROM lists: namespaces, data ranges, system variables. The bench 128K gets
	// two 16K ROM banks, 0 paged in; the lists name them by the MD5 of their bytes
	comp->mem->romMask = 2 * MEM_16K - 1;
	for (int i = 0; i < 2 * MEM_16K; i++) comp->mem->romData[i] = (unsigned char)(i * 7 + (i >> 8) + (i >> 14) * 3);
	QString md5 = QCryptographicHash::hash(QByteArray((const char*)comp->mem->romData, MEM_16K),
		QCryptographicHash::Md5).toHex();
	QString md5b = QCryptographicHash::hash(QByteArray((const char*)comp->mem->romData + MEM_16K, MEM_16K),
		QCryptographicHash::Md5).toHex();
	QDir(temp.path()).mkdir("labels");
	installDir = temp.path();
	const char* lists[][2] = {
		{"a.labels", "@ns SYSA\n@rom %1\n0010 ENTRY\n0020 ONLYA\n3D00 3D07 db\n0100 0103 text\n"},
		{"b.labels", "@ns SYSB\n@rom %1 -@0200 -@3e00\n0018 ENTRY\n0200 0201 dw\n0300 0301 dw\n3E00 3E01 db\n"},
		{"b2.labels", "@ns SYSA\n@rom %1\n0020 ONLYA\n"},
		{"c.labels", "@ns SYSA\n@ram 5\n5C3D ERR_SP\n5C3F LIST_SP\n"},
		{"d.labels", "@ns SYSC\n@ram 5\n5C00 NOT_FITTED\n"},
		{"e.labels", "@ns SYSD\n@rom %2\n0030 ONLYA\n0040 LATER\n"}};
	for (auto& lst : lists) {
		QFile out(temp.filePath(QString("labels/") + lst[0]));
		out.open(QIODevice::WriteOnly);
		out.write(QString(lst[1]).replace("%1", md5).replace("%2", md5b).toLatin1());
	}
	conf.dbg.romlabels = 1;
	conf.dbg.sysvars = 1;
	memSetBank(comp->mem, 0x00, MEM_ROM, 0, MEM_16K, NULL, NULL, NULL);
	labels_rom_scan(comp);
	xAdr r10 = comp_get_xadr(comp, 0x10, MEM_VIEW_READ);
	xAdr r20 = comp_get_xadr(comp, 0x20, MEM_VIEW_READ);
	check("ROM names in namespaces", find_label("SYSA.ENTRY").adr == 0x10 && find_label("SYSB.ENTRY").adr == 0x18
		&& find_label("ONLYA").adr == 0x20 && eval("SYSA.ENTRY").value == 0x10);
	check("a short name of two systems is neither",
		find_label("ENTRY").type < 0 && eval("ENTRY").err && labels_ambiguous("ENTRY").size() == 2);
	check("rows show the short name where it is enough", find_label(r20) == "ONLYA" && find_label(r10) == "SYSA.ENTRY"
		&& find_label(r10, nullptr, LAB_FULL) == "SYSA.ENTRY" && label_local_prefix("SYSA.ENTRY") == 4);
	xAdr ram = comp_get_xadr(comp, 0x8000, MEM_VIEW_READ);
	add_label(ram, "ONLYA");
	check("a label of the user's takes the short name", find_label("ONLYA").adr == 0x8000
		&& find_label(r20) == "SYSA.ONLYA" && find_label("SYSA.ONLYA").adr == 0x20);
	del_label("ONLYA");
	check("data ranges, one dropped on that bank", labels_rom_view(0x3D00) == DBG_VIEW_BYTE
		&& labels_rom_view(0x3D07) == DBG_VIEW_BYTE && labels_rom_view(0x3D08) == DBG_VIEW_CODE
		&& labels_rom_view(0x101) == DBG_VIEW_TEXT && labels_rom_view(0x200) == DBG_VIEW_CODE
		&& labels_rom_view(0x300) == DBG_VIEW_WORD && labels_rom_view(0x3E00) == DBG_VIEW_CODE);
	check("a name two lists of a bank share is one cell", labels_rom_cells("SYSA.ONLYA").size() == 1);
	// ONLYA is SYSA's in bank 0 and SYSD's in bank 1: the one paged in
	xExpr paged = xexpr_compile("ONLYA");
	check("a short name of several systems: the ROM paged in", labels_shared("ONLYA")
		&& find_label("ONLYA").adr == 0x20 && (xexpr_eval(paged, comp) == 0x20)
		&& find_label("LATER").type == MEM_ROM && find_label("LATER").adr < 0);
	xExpr fullx = xexpr_compile("SYSD.ONLYA");	// a full name's address, cached by the paging
	bool ferr = false;
	xexpr_eval(fullx, comp, &ferr);
	memSetBank(comp->mem, 0x00, MEM_ROM, 1, MEM_16K, NULL, NULL, NULL);
	bool ferr2 = false;
	check("a ROM name's address follows the paging", ferr && (xexpr_eval(fullx, comp, &ferr2) == 0x30) && !ferr2);
	check("...and after the paging changes", find_label("ONLYA").adr == 0x30 && (xexpr_eval(paged, comp) == 0x30)
		&& find_label(comp_get_xadr(comp, 0x30, MEM_VIEW_READ)) == "ONLYA"
		&& find_label(comp_get_xadr(comp, 0x40, MEM_VIEW_READ)) == "LATER");
	// neither paged in (RAM at 0000): compiles, and in a condition it equals no
	// address, the rest of the condition as written
	memSetBank(comp->mem, 0x00, MEM_RAM, 0, MEM_16K, NULL, NULL, NULL);
	comp->cpu->regPC = 0;
	xExpr none = xexpr_compile("PC == ONLYA");
	xExpr ne = xexpr_compile("PC != ONLYA");
	xExpr either = xexpr_compile("PC == 0 || PC == ONLYA");
	bool nerr = false, nout = false;
	unsigned nres = xexpr_eval(none, comp, &nerr, 0, 0, 0, &nout);
	check("...with none paged in: compiled, no address", xexpr_ok(none) && !nerr && nout && (nres == 0)
		&& (labels_candidates("ONLYA").size() == 2) && (labels_ambiguous("ONLYA").size() == 2));
	nerr = nout = false;
	check("...each comparison as written", (xexpr_eval(ne, comp, &nerr, 0, 0, 0, &nout) == 1)
		&& (xexpr_eval(either, comp, &nerr, 0, 0, 0, &nout) == 1) && !nerr && nout);
	nerr = false;
	xexpr_eval(none, comp, &nerr);
	check("...an error where nothing asks for that", nerr);
	// a label of the user's gone: the condition is broken, not false
	add_label(comp_get_xadr(comp, 0x8100, MEM_VIEW_READ), "GONE");
	xExpr gone = xexpr_compile("PC == GONE");
	del_label("GONE");
	nerr = nout = false;
	xexpr_eval(gone, comp, &nerr, 0, 0, 0, &nout);
	check("a label no longer there is an error", xexpr_ok(gone) && nerr && !nout);
	memSetBank(comp->mem, 0x00, MEM_ROM, 0, MEM_16K, NULL, NULL, NULL);
	nerr = nout = false;
	check("...and paged in again", (xexpr_eval(paged, comp, &nerr, 0, 0, 0, &nout) == 0x20) && !nerr && !nout);
	xAdr var = comp_get_xadr(comp, 0x5C3D, MEM_VIEW_READ);
	check("system variables in operands only", find_label(var).isEmpty()
		&& find_label(var, nullptr, LAB_SYSVAR) == "ERR_SP" && find_label(var, nullptr, LAB_SYSVAR | LAB_FULL) == "SYSA.ERR_SP"
		&& eval("ERR_SP").value == 0x5C3D && eval("SYSA.ERR_SP").value == 0x5C3D);
	xAdr high = var;
	high.adr = 0xDC3D;		// bank 5 paged at C000 as well
	check("a system variable only at its own address", find_label(high, nullptr, LAB_SYSVAR).isEmpty()
		&& !labels_sys_size(high));
	check("a system variable's size, to the next one", labels_sys_size(var) == 2
		&& labels_sys_size(comp_get_xadr(comp, 0x5C3F, MEM_VIEW_READ)) == 2);
	check("a system not in the machine names nothing", find_label("NOT_FITTED").type < 0
		&& find_label("SYSC.NOT_FITTED").type < 0);
	conf.dbg.sysvars = 0;
	check("system variables switched off", find_label(var, nullptr, LAB_SYSVAR).isEmpty() && find_label("ERR_SP").type < 0);
	conf.dbg.romlabels = 0;
	check("ROM labels switched off", find_label(r20).isEmpty() && labels_rom_view(0x3D00) == DBG_VIEW_CODE);
	while (!conf.labsets.isEmpty()) delLabelSet(conf.labsets.first()->name);
	compDestroy(comp);
	conf.zx = nullptr;
	return failures ? 1 : 0;
}
