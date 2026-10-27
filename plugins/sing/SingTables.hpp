// pump SING: the lyric tables (generated from Thunder Plus's VoxCore: the letter-to-sound rules after the
// public-domain 1976 US Naval Research Lab rules, and a small dictionary of common words). Do not edit by hand.
#pragma once
namespace pump { namespace sing {
static const char* const kRulesSrc = R"(A: |ATION||EY SH AX N; _|A|_|AX; _|ARE|_|AA R; _|AR|O|AX R; |AR|#|EH R; ^|AS|#|EY S; |A|WA|AX; |AW||AO; _:|ANY||EH N IY; |A|^+#|EY;
 #:|ALLY||AX L IY; _|AL|#|AX L; |AGAIN||AX G EH N; #:|AG|E|IH JH; |A|^+:#|AE; _:|A|^+_|EY; |A|^%|EY; _|ARR||AX R; |ARR||AE R;
 _:|AR|_|AA R; |AR|_|ER; |AR||AA R; |AIR||EH R; |AI||EY; |AY||EY; |AU||AO; #:|AL|_|AX L; #:|ALS|_|AX L Z; |ALK||AO K; |AL|^|AO L;
 _:|ABLE||EY B AX L; |ABLE||AX B AX L; |ANG|+|EY N JH; |A||AE
B: _|BE|^#|B IH; |BEING||B IY IH NG; _|BOTH|_|B OW TH; _|BUS|#|B IH Z; |BUIL||B IH L; |BB||B; |B||B
C: _|CH|^|K; ^E|CH||K; |CH||CH; _S|CI|#|S AY; |CI|A|SH; |CI|O|SH; |CI|EN|SH; |C|+|S; |CK||K; |COM|%|K AH M; |CC|+|K S; |CC||K; |C||K
D: #:|DED|_|D IH D; .E|D|_|D; #:^E|D|_|T; _|DE|^#|D IH; _|DO|_|D UW; _|DOES||D AH Z; _|DOING||D UW IH NG; _|DOW||D AW; |DU|A|JH UW;
 |DGE||JH; |DD||D; |D||D
E: #:|E|_|; _:|E|_|IY; #|ED|_|D; #:|E|D_|; |EV|ER|EH V; |E|^%|IY; |ERI|#|IY R IY; |ERI||EH R IH; #:|ER|#|ER; |ER|#|EH R; |ER||ER;
 _|EVEN||IY V EH N; #:|E|W|; @|EW||UW; |EW||Y UW; |E|O|IY; #:&|ES|_|IH Z; #:|E|S_|; #:|ELY|_|L IY; #:|EMENT||M EH N T; |EFUL||F UH L;
 |EE||IY; |EARN||ER N; _|EAR|^|ER; |EAD||EH D; #:|EA|_|IY AX; |EA|SU|EH; |EA||IY; |EIGH||EY; |EI||IY; _|EYE||AY; |EY||IY; |EU||Y UW;
 |E||EH
F: |FUL||F UH L; |FF||F; |F||F
G: |GIV||G IH V; _|G|I^|G; |GE|T|G EH; SU|GGES||G JH EH S; |GG||G; _B#|G||G; |G|+|JH; |GREAT||G R EY T; #|GH||; |GH||G; |G||G
H: _|HAV||HH AE V; _|HERE||HH IY R; _|HOUR||AW ER; |HOW||HH AW; |H|#|HH; |H||
I: _|IN||IH N; _|I|_|AY; |IN|D|AY N; |IER||IY ER; #:R|IED||IY D; |IED|_|AY D; |IEN||IY EH N; |IE|T|AY EH; _:|I|%|AY; |I|%|IY; |IE||IY;
 |I|^+:#|IH; |IR|#|AY R; |IZ|%|AY Z; |IS|%|AY Z; |I|D%|AY; +^|I|^+|IH; |I|T%|AY; #:^|I|^+|IH; |I|^+|AY; |IR||ER; |IGH||AY; |ILD||AY L D;
 |IGN|_|AY N; |IGN|^|AY N; |IGN|%|AY N; |IQUE||IY K; |I||IH
J: |J||JH
K: _|K|N|; |K||K
L: |LO|C#|L OW; L|L||; #:^|L|%|AX L; |LEAD||L IY D; |L||L
M: |MOV||M UW V; |MM||M; |M||M
N: E|NG|+|N JH; |NG|R|NG G; |NG|#|NG; |NGL|%|NG G AX L; |NG||NG; |NK||NG K; _|NOW|_|N AW; |NN||N; |N||N
O: |OF|_|AX V; |OROUGH||ER OW; #:|OR|_|ER; #:|ORS|_|ER Z; |OR||AO R; _|ONE||W AH N; |OW||OW; _|OVER||OW V ER; |OV||AH V; |O|^%|OW;
 |O|^EN|OW; |O|^I#|OW; |OL|D|OW L; |OUGHT||AO T; |OUGH||AH F; _|OU||AW; H|OU|S#|AW; |OUS||AX S; |OUR||AO R; |OULD||UH D;
 ^|OU|^L|AH; |OUP||UW P; |OU||AW; |OY||OY; |OING||OW IH NG; |OI||OY; |OOR||AO R; |OOK||UH K; |OOD||UH D; |OO||UW; |O|E|OW;
 |O|_|OW; |OA||OW; _|ONLY||OW N L IY; _|ONCE||W AH N S; |ON'T||OW N T; C|O|N|AA; |O|NG|AO; _:^|O|N|AH; I|ON||AX N; #:|ON|_|AX N;
 #^|ON||AX N; |O|ST_|OW; |OF|^|AO F; |OTHER||AH DH ER; |OSS|_|AO S; #:^|OM||AH M; |O||AA
P: |PH||F; |PEOP||P IY P; |POW||P AW; |PUT|_|P UH T; |PP||P; |P||P
Q: |QUAR||K W AO R; |QU||K W; |Q||K
R: _|RE|^#|R IY; |RR||R; |R||R
S: |SH||SH; #|SION||ZH AX N; |SOME||S AH M; #|SUR|#|ZH ER; |SUR|#|SH ER; #|SU|#|ZH UW; #|SSU|#|SH UW; #|SED|_|Z D; #|S|#|Z; |SAID||S EH D;
 ^|SION||SH AX N; |S|S|; .|S|_|Z; #:.E|S|_|Z; #:^#|S|_|S; U|S|_|S; _:#|S|_|Z; _|SCH||S K; |S|C+|; #|SM||Z M; #|SN|'|Z AX N; |S||S
T: _|THE|_|DH AX; |TO|_|T UW; |THAT|_|DH AE T; _|THIS|_|DH IH S; _|THEY||DH EY; _|THERE||DH EH R; |THER||DH ER; |THEIR||DH EH R;
 _|THAN|_|DH AE N; _|THEM|_|DH EH M; |THESE|_|DH IY Z; _|THEN||DH EH N; |THROUGH||TH R UW; |THOSE||DH OW Z; |THOUGH|_|DH OW;
 _|THUS||DH AH S; |TH||TH; #:|TED|_|T IH D; S|TI|#N|CH; |TI|O|SH; |TI|A|SH; |TIEN||SH AX N; |TUR|#|CH ER; |TU|A|CH UW; _|TWO||T UW;
 |TCH||CH; |TT||T; |T||T
U: _|UN|I|Y UW N; _|UN||AH N; _|UPON||AX P AO N; @|UR|#|UH R; |UR|#|Y UH R; |UR||ER; |U|^_|AH; |U|^^|AH; |UY||AY; _G|U|#|;
 G|U|%|; G|U|#|W; #N|U||Y UW; @|U||UW; |U||Y UW
V: |VIEW||V Y UW; |V||V
W: _|WERE||W ER; |WA|S|W AA; |WA|T|W AA; |WHERE||W EH R; |WHAT||W AA T; |WHOL||HH OW L; |WHO||HH UW; |WH||W; |WAR||W AO R;
 |WOR|^|W ER; |WR||R; |W||W
X: |X||K S
Y: |YOUNG||Y AH NG; _|YOU||Y UW; _|YES||Y EH S; _|Y|#|Y; #:^|Y|_|IY; #:^|Y|I|IY; _:|Y|_|AY; _:|Y|#|AY; _:|Y|^+:#|IH; _:|Y|^#|AY; |Y||IH
Z: |ZZ||Z; |Z||Z)";
static const char* const kDictSrc = R"(a:AX|the:DH AX|and:AE N D|to:T UW|of:AH V|you:Y UW|i:AY|is:IH Z|are:AA R|was:W AA Z|were:W ER|be:B IY|been:B IH N|
have:HH AE V|has:HH AE Z|had:HH AE D|do:D UW|does:D AH Z|did:D IH D|done:D AH N|said:S EH D|says:S EH Z|one:W AH N|two:T UW|
once:W AH N S|what:W AH T|who:HH UW|where:W EH R|there:DH EH R|their:DH EH R|they:DH EY|them:DH EH M|this:DH IH S|that:DH AE T|
with:W IH TH|from:F R AH M|your:Y AO R|our:AW ER|my:M AY|by:B AY|why:W AY|me:M IY|we:W IY|he:HH IY|she:SH IY|it:IH T|in:IH N|
on:AA N|at:AE T|so:S OW|no:N OW|go:G OW|oh:OW|ooh:UW|ah:AA|la:L AA|na:N AA|hey:HH EY|yeah:Y EH AX|love:L AH V|live:L IH V|
give:G IH V|gone:G AO N|come:K AH M|some:S AH M|none:N AH N|very:V EH R IY|many:M EH N IY|any:EH N IY|eye:AY|eyes:AY Z|
heart:HH AA R T|night:N AY T|light:L AY T|right:R AY T|bright:B R AY T|fly:F L AY|sky:S K AY|high:HH AY|tonight:T AX N AY T|
today:T AX D EY|tomorrow:T AX M AA R OW|world:W ER L D|word:W ER D|work:W ER K|girl:G ER L|boy:B OY|baby:B EY B IY|
want:W AA N T|wanna:W AA N AX|gonna:G AA N AX|into:IH N T UW|only:OW N L IY|own:OW N|know:N OW|known:N OW N|knew:N UW|
new:N UW|through:TH R UW|though:DH OW|thought:TH AO T|brought:B R AO T|would:W UH D|could:K UH D|should:SH UH D|mind:M AY N D|
kind:K AY N D|find:F AY N D|time:T AY M|fire:F AY ER|again:AX G EH N|people:P IY P AX L|woman:W UH M AX N|women:W IH M AX N|
machine:M AX SH IY N|robot:R OW B AA T|robots:R OW B AA T S|sing:S IH NG|song:S AO NG|dance:D AE N S|music:M Y UW Z IH K|
dream:D R IY M|dreams:D R IY M Z|feel:F IY L|real:R IY L|never:N EH V ER|ever:EH V ER|every:EH V R IY|forever:F ER EH V ER|
together:T AX G EH DH ER|alone:AX L OW N|home:HH OW M|move:M UW V|above:AX B AH V|human:HH Y UW M AX N|computer:K AX M P Y UW T ER|
hello:HH AX L OW|hi:HH AY|good:G UH D|bye:B AY|goodbye:G UH D B AY|am:AE M|an:AE N|or:AO R|for:F AO R|not:N AA T|but:B AH T|
all:AO L|can:K AE N|will:W IH L|just:JH AH S T|like:L AY K|now:N AW|how:HH AW|out:AW T|up:AH P|down:D AW N|day:D EY|way:W EY|
say:S EY|see:S IY|sea:S IY|look:L UH K|take:T EY K|make:M EY K|made:M EY D|walk:W AO K|talk:T AO K|call:K AO L|fall:F AO L|
stay:S T EY|play:P L EY|electric:IH L EH K T R IH K|digital:D IH JH IH T AX L|future:F Y UW CH ER|star:S T AA R|stars:S T AA R Z|
moon:M UW N|sun:S AH N|rain:R EY N|blue:B L UW|red:R EH D|gold:G OW L D|soul:S OW L|body:B AA D IY|voice:V OY S|
i'm:AY M|you're:Y UH R|it's:IH T S|don't:D OW N T|can't:K AE N T|won't:W OW N T|i'll:AY L|we're:W IH R|that's:DH AE T S|
let's:L EH T S|ain't:EY N T|there's:DH EH R Z|isn't:IH Z AX N T|didn't:D IH D AX N T|i've:AY V|you'll:Y UW L|nothing:N AH TH IH NG|
something:S AH M TH IH NG|everything:EH V R IY TH IH NG|everybody:EH V R IY B AA D IY|sometimes:S AH M T AY M Z|
because:B IH K AH Z|before:B IH F AO R|believe:B IH L IY V|beautiful:B Y UW T IH F AX L|friend:F R EH N D|friends:F R EH N D Z|
cold:K OW L D|old:OW L D|warm:W AO R M|lost:L AO S T|last:L AE S T|first:F ER S T|space:S P EY S|wave:W EY V|waves:W EY V Z|
thunder:TH AH N D ER|seeds:S IY D Z|yes:Y EH S|ok:OW K EY|okay:OW K EY)";
}} // namespace pump::sing
