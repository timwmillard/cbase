const char gui_xml_data[] =
	"<Window Title=\"People\" Width=\"420\" Height=\"480\">\n"
	"  <VBox Spacing=\"8\" Slot.Padding=\"12\">\n"
	"    <HBox Spacing=\"8\">\n"
	"      <TextInput Name=\"name\" Placeholder=\"Name\" Slot.Fill=\"1\"/>\n"
	"      <SpinBox Name=\"age\" Min=\"0\" Max=\"150\" Value=\"30\" Decimals=\"0\" Width=\"90\"/>\n"
	"      <Button Text=\"Add\" Style=\"Primary\" OnClicked=\"person.add\"/>\n"
	"    </HBox>\n"
	"    <ListView Name=\"people\" Slot.Fill=\"1\"/>\n"
	"    <HBox Spacing=\"8\">\n"
	"      <Label Name=\"status\" Opacity=\"0.6\" Slot.Fill=\"1\" Slot.VAlign=\"Center\"/>\n"
	"      <Button Text=\"Delete\" OnClicked=\"person.delete\"/>\n"
	"    </HBox>\n"
	"  </VBox>\n"
	"</Window>\n"
	"";
const unsigned int gui_xml_len = 567;
