# Generating an SSH key

```
ssh-keygen -t ed25519
eval "$(ssh-agent -s)"
ssh-add ~/.ssh/<YOUR KEY HERE>
```

**!!! Your public key is at `~/.ssh/<YOUR KEY HERE>.pub` !!!**

**DO NOT SEND US YOUR PRIVATE KEY**
