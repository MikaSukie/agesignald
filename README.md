# agesignald
### Made by a 16 year old btw 💀 
I am utilizing the Bhumi compiler and build system that I designed at
```
https://github.com/MikaSukie/Bhumi
```
(I try constantly to learn programming. I started by using tools to make a compiler, then I try learning using my compiler.)
I am doing my best to reduce my reliance on such tools. 
I would say, manually designing each feature and implememnting them using the tools have been a very helpful resource.
Anyway, here is my attempt to try saving digital privacy using what I know.
## I hope it does turn out to be helpful. </br>

Just a fair warning, This project intentionally implements the minimum possible interpretation of an OS-level age signal (so lawmakers can not assume that extra sensitive information is easy to implement as an update). </br>
It stores only a locally computed age bracket (via localhost) and serves it to local apps. Nothing else. </br>

### The shipped release is a binary for x86-64 (will need manual .service creation). </br>

## Where are they stored?
```
$XDG_DATA_HOME/age-signal/age_bracket
```
## If the ENV variable is empty, it will be stored in
```
~/.local/share/age-signal/age_bracket
```

## To see your age bracket (After setting it)
```bash
cat ~/.local/share/age-signal/age_bracket
```

## Clearing/Resetting the age bracket is as simple as
```bash
rm ~/.local/share/age-signal/age_bracket
```

## Health checking (GET /health) :
```bash
curl http://127.0.0.1:8080/health
```

## To set an age (GET /set_age) :
```bash
curl "http://127.0.0.1:8080/set_age?birthdate=yyyy-mm-dd"
```

## To get the age signal (GET /get_age_signal) :
```bash
curl "http://127.0.0.1:8080/get_age_signal?app=com.example.test"
```

## Example response would be
```json
{
  "age_bracket": "16_to_17",
  "issued_at": "2026-03-03T21:02:14Z",
  "scope": "com.example.foo"
}
```

The issued date will be stored in the ISO 8601 UTC format.
## possible age brackets are the following:
```
under_13
13_to_15
16_to_17
18_plus
```
