export interface HardwareMetadataBase {
  id: string;
  name: string;
  type: "board" | "shield" | "interconnect";
  url?: string;
  features?: string[];
  siblings?: string[];
}

export interface Board extends HardwareMetadataBase {
  type: "board";
  exposes?: string[];
}

export interface Shield extends HardwareMetadataBase {
  type: "shield";
  requires: string[];
  exposes?: string[];
}

export interface Interconnect extends HardwareMetadataBase {
  type: "interconnect";
  description?: string;
  design_guideline?: string;
  node_labels?: {
    adc?: string;
    i2c?: string;
    spi?: string;
    uart?: string;
  };
}

export type HardwareMetadata = Board | Shield | Interconnect;
