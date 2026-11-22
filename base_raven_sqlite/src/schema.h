const char schema_data[] =
	"\n"
	"create table if not exists person (\n"
	"    id integer primary key,\n"
	"    name text not null default '',\n"
	"    age integer not null default 0\n"
	");\n"
	"\n"
	"create table if not exists pet (\n"
	"    id integer primary key,\n"
	"    name text not null default '',\n"
	"    owner_id integer references person(id)\n"
	");\n"
	"";
const unsigned int schema_len = 281;
