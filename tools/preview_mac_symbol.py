"""Render the production vector symbol at 1x/2x plus a nearest-pixel review."""
from pathlib import Path
import subprocess
from storage_hygiene import scratch_directory
ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'test-results/helper-icons-0.2.4'
OUT.mkdir(parents=True,exist_ok=True)
swift=r'''
import Cocoa
let app=NSApplication.shared
let output=URL(fileURLWithPath:CommandLine.arguments[1])
func bitmap(_ w:Int,_ h:Int)->NSBitmapImageRep {
 NSBitmapImageRep(bitmapDataPlanes:nil,pixelsWide:w,pixelsHigh:h,bitsPerSample:8,samplesPerPixel:4,hasAlpha:true,isPlanar:false,colorSpaceName:.deviceRGB,bytesPerRow:0,bitsPerPixel:0)!
}
func save(_ rep:NSBitmapImageRep,_ name:String){try! rep.representation(using:.png,properties:[:])!.write(to:output.appendingPathComponent(name))}
var icons=[RemoteConnection:[CGImage]]()
for state in RemoteConnection.allCases {
 icons[state]=[]
 for scale in [1,2] {
  let rep=bitmap(22*scale,18*scale)
  NSGraphicsContext.saveGraphicsState();NSGraphicsContext.current=NSGraphicsContext(bitmapImageRep:rep)
  NSGraphicsContext.current!.cgContext.scaleBy(x:CGFloat(scale),y:CGFloat(scale))
  dittoSymbol(connection:state).draw(in:NSRect(x:0,y:0,width:22,height:18),from:.zero,operation:.copy,fraction:1)
  NSGraphicsContext.restoreGraphicsState();save(rep,"\(state.rawValue)-\(scale)x.png");icons[state]!.append(rep.cgImage!)
 }
}
let review=bitmap(600,420)
NSGraphicsContext.saveGraphicsState();NSGraphicsContext.current=NSGraphicsContext(bitmapImageRep:review)
let c=NSGraphicsContext.current!.cgContext
func label(_ text:String,_ x:CGFloat,_ y:CGFloat,_ color:NSColor,_ size:CGFloat=12){
 (text as NSString).draw(at:NSPoint(x:x,y:y),withAttributes:[.font:NSFont.systemFont(ofSize:size),.foregroundColor:color])
}
func icon(_ state:RemoteConnection,_ index:Int,_ r:CGRect,_ color:NSColor){
 c.saveGState();c.interpolationQuality = .none;c.clip(to:r,mask:icons[state]![index]);c.setFillColor(color.cgColor);c.fill(r);c.restoreGState()
}
for theme in 0..<2 {
 let y=CGFloat(theme*210),fg:NSColor=theme==1 ? .white:.black
 NSColor(calibratedWhite:theme==1 ? 0.12:0.94,alpha:1).setFill();NSRect(x:0,y:y,width:600,height:210).fill()
 for (column,state) in RemoteConnection.allCases.enumerated() {
  let x=CGFloat(column*200)
  label(state.label,x+24,y+180,fg)
  icon(state,0,CGRect(x:x+30,y:y+138,width:22,height:18),fg)
  icon(state,1,CGRect(x:x+78,y:y+138,width:22,height:18),fg)
  label("1×       2×",x+28,y+119,fg,10)
  icon(state,1,CGRect(x:x+28,y:y+12,width:110,height:90),fg)
 }
}
NSGraphicsContext.restoreGraphicsState();save(review,"symbol-review.png")

'''
with scratch_directory('menu-symbol',reserve_mib=32) as scratch:
 p=Path(scratch);(p/'main.swift').write_text(swift)
 subprocess.run(['swiftc','-O','-framework','Cocoa',str(ROOT/'host/mac-remote/Menu.swift'),str(p/'main.swift'),'-o',str(p/'render')],check=True)
 subprocess.run([str(p/'render'),str(OUT)],check=True)
print(OUT/'symbol-review.png')
