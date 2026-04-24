import os
import glob

# Đường dẫn trỏ tới thư mục chứa các file dump mà bạn đã tải về (đã sửa cho WSL)
crash_dir = r"/mnt/c/Users/Admin/Downloads/Compressed/AppCrash_NexusKeyClassic"

# Tìm tất cả file Report.wer trong các thư mục con
wer_files = glob.glob(os.path.join(crash_dir, "**", "Report.wer"), recursive=True)

if not wer_files:
    print(f"Không tìm thấy file Report.wer nào trong {crash_dir}")
else:
    print(f"Tìm thấy {len(wer_files)} file crash report. Đang phân tích...\n")
    
    for file_path in wer_files:
        folder_name = os.path.basename(os.path.dirname(file_path))
        print(f"=== Report: {folder_name} ===")
        try:
            # File .wer luôn được lưu dưới dạng UTF-16 Little Endian
            with open(file_path, "r", encoding="utf-16-le") as f:
                lines = f.read().splitlines()
                
            sigs = {}
            for line in lines:
                if line.startswith("Sig["):
                    parts = line.split("=", 1)
                    if len(parts) == 2:
                        key = parts[0]   # vd: Sig[3].Name
                        val = parts[1]   # vd: Exception Code
                        
                        idx = key.split("]")[0].replace("Sig[", "")
                        prop = key.split(".")[1] # Name hoặc Value
                        
                        if idx not in sigs:
                            sigs[idx] = {}
                        sigs[idx][prop] = val
                        
            # In ra các thông số quan trọng liên quan đến nguyên nhân crash
            for idx, data in sigs.items():
                name = data.get("Name", "")
                value = data.get("Value", "")
                if any(k in name for k in ["Exception Code", "Exception Offset", "Fault Module Name", "Fault Module Version"]):
                    print(f"{name.ljust(25)}: {value}")
            print("\n")
                    
        except Exception as e:
            print(f"Lỗi khi đọc file: {e}")
