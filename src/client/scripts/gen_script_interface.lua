--- A script to generate the client's lua script interfaces
--- args:
---     prefix - The prefix used for the functions in the input header file (e.g GF_SCRIPt, QF_SRIPT or SYSTEM_SCRIPT)
---     in_file - Input file to generate lua interfaces for. Most be formatted appropriately (see below for details)
---     out_def_file - Output file to generate the definitions
---     out_reg_file - Output file to register the interfaces
function read_interfaces ( prefix, hin, hout )
    i = 1
    word = read( hin, "*w" )

    while ( word ) do
        if (word ~= prefix) then
            word = read( hin, "*l" ) -- skip current line
        else
            local return_type
            local name
            return_type = read( hin, "*w" )
            if (not return_type) then break end

            name = read( hin, "*w" )
            if (not name) then break end

            interfaces[name] = {}

            interfaces[name].return_type = return_type

            word = read( hin, "*w" ) -- skip (
            if (not word) then break end

            word = read( hin, "*w" )
            if (not word) then break end

            interfaces[name].params = {}
            local param_index = 1
            
            while (word ~= ");") do
                -- extract out comma(,)
                interfaces[name].params[param_index] = gsub( word, ",", "" )
                word = read( hin, "*w" ) -- skip parameter name
                if (not word) then break end
                if (word == ");") then break end
                word = read( hin, "*w" ) -- read next
                if (not word) then break end
                param_index = param_index + 1
            end
            i = i + 1
        end -- end of if
        word = read( hin, "*w" ) -- read next
    end -- end of while
end

PREFIX = arg[1]
IN_FILENAME = arg[2]
OUT_FILENAME1 = arg[3]
OUT_FILENAME2 = arg[4]

-- Hacky way to create intermediate directories
remove(OUT_FILENAME1)
remove(OUT_FILENAME2)
execute("mkdir " .. OUT_FILENAME1)
execute("rmdir " .. OUT_FILENAME1)

hin   = readfrom ( IN_FILENAME   ) -- input file handle
hout1 = writeto  ( OUT_FILENAME1 ) -- output file handle
hout2 = writeto ( OUT_FILENAME2 ) -- output file handle

interfaces = {} -- �������̽� ���� ���̺�

--- �������̽� ���� �о���̱�
read_interfaces ( PREFIX, hin, hout )

names = {} --- ���Ŀ� �̸� ���̺�
i = 1
index = next(interfaces, nil) -- �̸����� �����ϴ� ���̺��̹Ƿ�, interfaces[i] �� ����� �� ����.
while (index) do
    names[i] = index
    index = next(interfaces, index)
    i = i + 1
end

--- �̸������� �����ϱ�
sort( names )

--- ��ũ�� ���� ���̺���
map_return_types = {
    void    = "RETURNS_NONE",
    int     = "RETURNS_INT",
    float   = "RETURNS_FLOAT",
    HNODE   = "RETURNS_HNODE",
    ZSTRING = "RETURNS_STRING"
}
map_param_types = {
    void    = "",
    int     = "ZL_INT",
    float   = "ZL_FLOAT",
    HNODE   = "ZL_HNODE",
    ZSTRING = "ZL_STRING"
}

--- ��� �޼���
message_autogen =
"//==========================================================================//\n"..
"// This file was automatically generated. Do not modify!\n"..
"// Generated: "..date().."\n"..
"//==========================================================================//\n\n\n"

write( hout1, message_autogen )
write( hout2, message_autogen )

--- ��ũ��Ʈ �������̽� ���ǿ� ���� �����ϱ�
for i = 1, getn(names) do
    definition = format( "ZL_FUNC_BEGIN( %s, ", names[i] )
    -- export return_type
    definition = definition..format( "%s", map_return_types[ interfaces[names[i]].return_type ] )
    definition = definition.." )\n"
    
    for j = 1, getn(interfaces[names[i]].params) do
        if (j == getn(interfaces[names[i]].params)) then
            definition = definition..format( "\t%15s\n", map_param_types[ interfaces[names[i]].params[j] ] )
        else
            definition = definition..format( "\t%15s,\n", map_param_types[ interfaces[names[i]].params[j] ] )
        end
    end

    definition = definition.."ZL_FUNC_END\n\n"
        
    write( hout1, definition )
end

--- ��ũ��Ʈ �������̽� ��Ͽ� ���� �����ϱ�
for i = 1, getn(names) do
    register = format( "ZL_REGISTER( %-30s )\n", names[i] )
    write( hout2, register )
end

--- ������ �� ǥ��
message_end_of_file = "// end of file\n"

write( hout1, message_end_of_file )
write( hout2, message_end_of_file )

closefile( hin   )
closefile( hout1 )
closefile( hout2 )

