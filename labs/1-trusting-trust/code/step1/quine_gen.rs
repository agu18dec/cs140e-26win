use std::io::{self, Read, Write};

fn main() -> io::Result<()> {
    let mut input = Vec::new();
    io::stdin().read_to_end(&mut input)?;
    let mut out = io::BufWriter::new(io::stdout());
    writeln!(out, "char prog[] = {{")?;
    for (i, b) in input.iter().enumerate() {
        write!(out, "\t{},{}", *b as u32, if (i + 1) % 8 == 0 { '\n' } else { ' ' })?;
    }
    writeln!(out, "0 }};")?;
    out.write_all(&input)?;
    out.flush()?;
    Ok(())
}
