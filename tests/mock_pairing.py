"""Local protocol integration: PIN proof, mutual TLS, pin rejection, chunked XML.
No real Sunshine host or user credentials are used. Private fixtures stay in work/.
"""
import datetime,hashlib,http.server,json,pathlib,secrets,ssl,subprocess,threading,time,urllib.parse
from cryptography import x509
from cryptography.hazmat.primitives import hashes,serialization,padding as sympadding
from cryptography.hazmat.primitives.asymmetric import rsa,padding
from cryptography.hazmat.primitives.ciphers import Cipher,algorithms,modes
from cryptography.x509.oid import NameOID

ROOT=pathlib.Path(__file__).resolve().parents[1]
PRIVATE=ROOT/'work/test-private';PRIVATE.mkdir(exist_ok=True)
def identity(label):
 key=rsa.generate_private_key(public_exponent=65537,key_size=2048)
 subject=x509.Name([x509.NameAttribute(NameOID.COMMON_NAME,label)])
 now=datetime.datetime.now(datetime.timezone.utc)
 cert=(x509.CertificateBuilder().subject_name(subject).issuer_name(subject).public_key(key.public_key())
       .serial_number(x509.random_serial_number()).not_valid_before(now-datetime.timedelta(days=1))
       .not_valid_after(now+datetime.timedelta(days=365)).add_extension(x509.BasicConstraints(ca=True,path_length=None),critical=True).sign(key,hashes.SHA256()))
 pem=cert.public_bytes(serialization.Encoding.PEM);certfile=PRIVATE/(label+'.pem');keyfile=PRIVATE/(label+'.key')
 certfile.write_bytes(pem);keyfile.write_bytes(key.private_bytes(serialization.Encoding.PEM,serialization.PrivateFormat.TraditionalOpenSSL,serialization.NoEncryption()))
 return key,cert,pem,certfile,keyfile
server_key,server_cert,server_pem,certfile,keyfile=identity('mock-sunshine')
wrong_key,wrong_cert,wrong_pem,wrong_certfile,wrong_keyfile=identity('wrong-tls-server')
ctx=ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER);ctx.minimum_version=ssl.TLSVersion.TLSv1_2
ctx.load_cert_chain(certfile,keyfile);ctx.verify_mode=ssl.CERT_REQUIRED
states={};events=[];pin=b'1234'
def aes(key,data,encrypt):
 op=Cipher(algorithms.AES(key),modes.ECB()).encryptor() if encrypt else Cipher(algorithms.AES(key),modes.ECB()).decryptor()
 return op.update(data)+op.finalize()
def xml(inner):return ('<?xml version="1.0"?><root status_code="200">'+inner+'</root>').encode()
class Handler(http.server.BaseHTTPRequestHandler):
 protocol_version='HTTP/1.1'
 def log_message(self,*args):pass
 def respond(self,data,status=200,chunked=False):
  self.send_response(status);self.send_header('Content-Type','application/xml; charset=utf-8');self.send_header('Connection','close')
  if chunked:self.send_header('Transfer-Encoding','chunked')
  else:self.send_header('Content-Length',str(len(data)))
  self.end_headers()
  if chunked:
   for i in range(0,len(data),23):
    part=data[i:i+23];self.wfile.write(f'{len(part):x}\r\n'.encode()+part+b'\r\n')
   self.wfile.write(b'0\r\n\r\n')
  else:self.wfile.write(data)
  self.close_connection=True
 def do_GET(self):
  try:
   u=urllib.parse.urlsplit(self.path);q={k:v[-1] for k,v in urllib.parse.parse_qs(u.query).items()};uid=q.get('uniqueid','');s=states.get(uid,{})
   tls=isinstance(self.connection,ssl.SSLSocket);events.append({'path':u.path,'tls':tls})
   if tls:
    peer=self.connection.getpeercert(binary_form=True)
    if not s.get('paired') or peer!=s['client'].public_bytes(serialization.Encoding.DER):self.respond(xml('<paired>0</paired>'),403);return
   if u.path=='/serverinfo':
    self.respond(xml('<hostname>Local mock Sunshine</hostname><appversion>7.1.431.0</appversion><GfeVersion>3.23.0.74</GfeVersion>'
      f'<HttpsPort>{https_server.server_port}</HttpsPort><PairStatus>{1 if tls and s.get("paired") else 0}</PairStatus><state>MOONLIGHT_SERVER_FREE</state><currentgame>0</currentgame><ServerCodecModeSupport>1</ServerCodecModeSupport>'));return
   if u.path=='/unpair':states.pop(uid,None);self.respond(xml('<paired>0</paired>'));return
   if u.path=='/applist':
    if not tls:self.respond(b'',403);return
    self.respond(xml('<App><ID>1</ID><AppTitle>Desktop</AppTitle></App><App><ID>2</ID><AppTitle>测试 &amp; 应用</AppTitle></App>'),chunked=True);return
   if u.path!='/pair':self.respond(b'',404);return
   if q.get('phrase')=='getservercert':
    pem=bytes.fromhex(q['clientcert']);client=x509.load_pem_x509_certificate(pem);ctx.load_verify_locations(cadata=pem.decode())
    s={'client':client,'aes':hashlib.sha256(bytes.fromhex(q['salt'])+pin).digest()[:16],
       'server_secret':secrets.token_bytes(16),'server_challenge':secrets.token_bytes(16),'paired':False};states[uid]=s
    self.respond(xml('<paired>1</paired><plaincert>'+server_pem.hex()+'</plaincert>'));return
   if 'clientchallenge' in q:
    challenge=aes(s['aes'],bytes.fromhex(q['clientchallenge']),False)
    digest=hashlib.sha256(challenge+server_cert.signature+s['server_secret']).digest()
    self.respond(xml('<paired>1</paired><challengeresponse>'+aes(s['aes'],digest+s['server_challenge'],True).hex()+'</challengeresponse>'));return
   if 'serverchallengeresp' in q:
    s['client_hash']=aes(s['aes'],bytes.fromhex(q['serverchallengeresp']),False)
    signature=server_key.sign(s['server_secret'],padding.PKCS1v15(),hashes.SHA256())
    self.respond(xml('<paired>1</paired><pairingsecret>'+(s['server_secret']+signature).hex()+'</pairingsecret>'));return
   if 'clientpairingsecret' in q:
    value=bytes.fromhex(q['clientpairingsecret']);secret,signature=value[:16],value[16:]
    s['client'].public_key().verify(signature,secret,padding.PKCS1v15(),hashes.SHA256())
    assert s['client_hash']==hashlib.sha256(s['server_challenge']+s['client'].signature+secret).digest()
    s['paired']=True;self.respond(xml('<paired>1</paired>'));return
   if q.get('phrase')=='pairchallenge' and tls:self.respond(xml('<paired>1</paired>'));return
   self.respond(xml('<paired>0</paired>'))
  except Exception as e:
   events.append({'error':type(e).__name__});self.respond(xml('<paired>0</paired>'),400)

http_server=http.server.ThreadingHTTPServer(('127.0.0.1',0),Handler)
https_server=http.server.ThreadingHTTPServer(('127.0.0.1',0),Handler)
https_server.socket=ctx.wrap_socket(https_server.socket,server_side=True)
for server in (http_server,https_server):threading.Thread(target=server.serve_forever,daemon=True).start()
exe=ROOT/'work/build-x86_64/MoonlightRT81.exe';results=[]
try:
 for name,provided,expected in [('correct-pin','1234',0),('incorrect-pin','4321',1),('wrong-tls-certificate','1234',1)]:
  profile=PRIVATE/name;profile.mkdir(exist_ok=True)
  if name=='wrong-tls-certificate':ctx.load_cert_chain(wrong_certfile,wrong_keyfile)
  before=len(events)
  process=subprocess.run([str(exe),'--pair-test','127.0.0.1',str(http_server.server_port),provided,'--profile',str(profile)],timeout=45)
  report={'case':name,'exit_code':process.returncode,'expected_exit':expected,'pinned_host_files':len(list(profile.glob('host-*.pem'))),
          'request_types':events[before:]}
  assert process.returncode==expected,report
  assert report['pinned_host_files']==(1 if expected==0 else 0),report
  results.append(report);print(json.dumps(report),flush=True)
finally:
 http_server.shutdown();https_server.shutdown();http_server.server_close();https_server.server_close()
out=ROOT/'validation';out.mkdir(exist_ok=True)
(out/'mock-pairing-results.json').write_text(json.dumps({'cases':results,'real_sunshine_host_tested':False},indent=2)+'\n')
