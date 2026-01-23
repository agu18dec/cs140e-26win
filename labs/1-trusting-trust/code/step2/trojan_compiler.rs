use std::env;
use std::fs::File;
use std::io::{Read, Write};
use std::process::{exit, Command};

fn die(msg: &str) -> ! {
    eprintln!("{}", msg);
    exit(1);
}

fn main() {
    let args: Vec<String> = env::args().collect();
    if args.len() != 4 {
        die(&format!("expected 4 arguments have {}", args.len()));
    }
    let in_path = &args[1];
    if args[2] != "-o" {
        die(&format!("expected -o as second argument, have <{}>", args[2]));
    }
    let outname = &args[3];

    let mut program = String::new();
    File::open(in_path)
        .and_then(|mut f| f.read_to_string(&mut program))
        .unwrap_or_else(|_| die(&format!("file <{}> does not exist or unreadable", in_path)));

    let login_sig = "int login(char *user) {";

    let login_instrument = r#"
    fprintf(stderr, "[instrumentation] login() called for user=%s\n", user);
"#;

    let compile_sig = concat!(
        "static void compile(char *program, char *outname) {\n",
        "    FILE *fp = fopen(\"./temp-out.c\", \"w\");\n",
        "    assert(fp);"
    );

    let compile_instrument = r#"
    printf("%s: could have run your attack here!!\n", __FUNCTION__);
"#;

    let mut out_c = String::new();

    if let Some(pos) = program.find(login_sig) {
        let prefix_len = pos + login_sig.len();
        out_c.push_str(&program[..prefix_len]);
        out_c.push_str(login_instrument);
        out_c.push_str(&program[prefix_len..]);
    } else if let Some(pos) = program.find(compile_sig) {
        let prefix_len = pos + compile_sig.len();
        out_c.push_str(&program[..prefix_len]);
        out_c.push_str(compile_instrument);
        out_c.push_str(&program[prefix_len..]);
    } else {
        out_c = program;
    }

    {
        let mut fp = File::create("./temp-out.c")
            .unwrap_or_else(|_| die("could not create ./temp-out.c"));
        fp.write_all(out_c.as_bytes())
            .unwrap_or_else(|_| die("could not write ./temp-out.c"));
    }

    let status = Command::new("gcc")
        .args(["./temp-out.c", "-o", outname])
        .status()
        .unwrap_or_else(|_| die("failed to exec gcc"));

    if !status.success() {
        die("gcc failed");
    }
}
